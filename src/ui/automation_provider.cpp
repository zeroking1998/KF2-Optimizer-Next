#include "kf2/ui/automation_provider.hpp"

#include <ole2.h>
#include <UIAutomationCore.h>
#include <UIAutomationClient.h>
#include <UIAutomationCoreApi.h>
#include <oleauto.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kf2::ui {
namespace {

constexpr ProviderOptions kStaProviderOptions = static_cast<ProviderOptions>(
    ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);

bool current_thread_owns_sta() noexcept {
    APTTYPE apartment{};
    APTTYPEQUALIFIER qualifier{};
    if (FAILED(CoGetApartmentType(&apartment, &qualifier))) return false;
    return apartment == APTTYPE_STA || apartment == APTTYPE_MAINSTA;
}

struct Context;
class RootProvider;

struct ProviderIdentity {
    std::string id;
    SemanticRole role{SemanticRole::root};
    std::optional<Destination> destination;
    std::optional<std::string> action_id;
    bool slider{false};

    explicit ProviderIdentity(const SemanticNode& node)
        : id{node.id}, role{node.role}, destination{node.destination},
          action_id{node.action_id}, slider{node.slider.has_value()} {}

    [[nodiscard]] bool matches(const SemanticNode& node) const noexcept {
        return id == node.id && role == node.role &&
               destination == node.destination && action_id == node.action_id &&
               slider == node.slider.has_value();
    }
    [[nodiscard]] bool supports_invoke() const noexcept {
        return destination.has_value() ||
               (role == SemanticRole::action && action_id.has_value());
    }
};

template <typename Function>
HRESULT automation_boundary(Function&& function) noexcept {
    try {
        return std::forward<Function>(function)();
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

HRESULT string_value(VARIANT* value, std::wstring_view text) noexcept {
    VariantInit(value);
    value->vt = VT_BSTR;
    value->bstrVal = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
    return value->bstrVal == nullptr && !text.empty() ? E_OUTOFMEMORY : S_OK;
}

HRESULT integer_value(VARIANT* value, int number) {
    VariantInit(value);
    value->vt = VT_I4;
    value->lVal = number;
    return S_OK;
}

HRESULT boolean_value(VARIANT* value, bool state) {
    VariantInit(value);
    value->vt = VT_BOOL;
    value->boolVal = state ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

UiaRect screen_bounds(HWND window, const DipRect& bounds) {
    POINT origin{};
    ClientToScreen(window, &origin);
    const double scale = static_cast<double>(GetDpiForWindow(window)) / 96.0;
    return {origin.x + bounds.x * scale, origin.y + bounds.y * scale,
            bounds.width * scale, bounds.height * scale};
}

struct Context {
    HWND window{};
    UiModel* model{};
    ShellLayoutResult layout;
    RootProvider* root{};
    std::function<void(std::string_view)> activate_action;
    std::function<void()> invalidate;
    std::function<void(std::string_view, int)> set_slider_value;
    std::atomic_bool connected{true};
#if defined(KF2_AUTOMATION_PROVIDER_TESTING)
    bool fail_next_child_allocation{false};
#endif
};

class NodeProvider final : public IRawElementProviderSimple,
                           public IRawElementProviderFragment,
                           public IInvokeProvider,
                           public ISelectionItemProvider,
                           public IRangeValueProvider {
public:
    NodeProvider(std::shared_ptr<Context> context, const SemanticNode& node,
                 int runtime_id, std::size_t index)
        : context_{std::move(context)}, identity_{node},
          runtime_id_{runtime_id}, index_{index} {}

    IFACEMETHODIMP QueryInterface(REFIID id, void** object) override;
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG remaining = --references_;
        if (remaining == 0) delete this;
        return remaining;
    }
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions* options) override {
        if (!options) return E_POINTER;
        *options = kStaProviderOptions;
        return S_OK;
    }
    IFACEMETHODIMP GetPatternProvider(PATTERNID pattern, IUnknown** provider) override;
    IFACEMETHODIMP GetPropertyValue(PROPERTYID property, VARIANT* value) override;
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** provider) override {
        if (!provider) return E_POINTER;
        *provider = nullptr;
        return node() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    IFACEMETHODIMP Navigate(NavigateDirection direction,
                            IRawElementProviderFragment** provider) override;
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** runtime_id) override;
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* bounds) override {
        if (!bounds) return E_POINTER;
        *bounds = {};
        const auto* current = node();
        if (!current) return UIA_E_ELEMENTNOTAVAILABLE;
        *bounds = screen_bounds(context_->window, current->bounds);
        return S_OK;
    }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** roots) override {
        if (!roots) return E_POINTER;
        *roots = nullptr;
        return node() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    IFACEMETHODIMP SetFocus() override {
        return automation_boundary([&]() -> HRESULT {
            const auto* current = node();
            if (!current || !context_->model) {
                return UIA_E_ELEMENTNOTAVAILABLE;
            }
            if (current->destination) {
                (void)context_->model->focus_destination(*current->destination);
            } else if (current->action_id) {
                if (!current->enabled) return UIA_E_ELEMENTNOTENABLED;
                (void)context_->model->focus_action(*current->action_id);
            } else {
                return UIA_E_NOTSUPPORTED;
            }
            ::SetFocus(context_->window);
            if (context_->invalidate) context_->invalidate();
            return S_OK;
        });
    }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** root) override;
    IFACEMETHODIMP Invoke() override {
        return automation_boundary([&]() -> HRESULT {
            const auto* resolved = node();
            if (!resolved || !context_->model) {
                return UIA_E_ELEMENTNOTAVAILABLE;
            }
            const auto current = *resolved;
            if (current.destination) {
                (void)context_->model->focus_destination(*current.destination);
                (void)context_->model->activate_focused();
                if (context_->invalidate) context_->invalidate();
                return S_OK;
            }
            if (!current.action_id || !context_->activate_action) {
                return UIA_E_NOTSUPPORTED;
            }
            if (!current.enabled) return UIA_E_ELEMENTNOTENABLED;
            (void)context_->model->focus_action(*current.action_id);
            if (context_->invalidate) context_->invalidate();
            context_->activate_action(*current.action_id);
            return S_OK;
        });
    }
    IFACEMETHODIMP Select() override {
        if (!node()) return UIA_E_ELEMENTNOTAVAILABLE;
        return identity_.destination ? Invoke() : UIA_E_NOTSUPPORTED;
    }
    IFACEMETHODIMP AddToSelection() override {
        return node() ? UIA_E_INVALIDOPERATION : UIA_E_ELEMENTNOTAVAILABLE;
    }
    IFACEMETHODIMP RemoveFromSelection() override {
        return node() ? UIA_E_INVALIDOPERATION : UIA_E_ELEMENTNOTAVAILABLE;
    }
    IFACEMETHODIMP get_IsSelected(BOOL* selected) override {
        if (!selected) return E_POINTER;
        *selected = FALSE;
        const auto* current = node();
        if (!current) return UIA_E_ELEMENTNOTAVAILABLE;
        *selected = current->selected ? TRUE : FALSE;
        return S_OK;
    }
    IFACEMETHODIMP get_SelectionContainer(IRawElementProviderSimple** container) override;
    IFACEMETHODIMP SetValue(double value) override;
    IFACEMETHODIMP get_Value(double* value) override;
    IFACEMETHODIMP get_IsReadOnly(BOOL* read_only) override;
    IFACEMETHODIMP get_Maximum(double* maximum) override;
    IFACEMETHODIMP get_Minimum(double* minimum) override;
    IFACEMETHODIMP get_LargeChange(double* change) override;
    IFACEMETHODIMP get_SmallChange(double* change) override;

    [[nodiscard]] std::string_view id() const noexcept { return identity_.id; }
    [[nodiscard]] bool matches_identity(const SemanticNode& node) const noexcept {
        return identity_.matches(node);
    }
    void set_index(std::size_t index) noexcept { index_ = index; }
    void deactivate() noexcept { active_ = false; }

private:
    const SemanticNode* node() const noexcept {
        if (!active_ || !context_->connected ||
            index_ + 1 >= context_->layout.nodes.size()) {
            return nullptr;
        }
        const auto& current = context_->layout.nodes[index_ + 1];
        return identity_.matches(current) ? &current : nullptr;
    }
    std::atomic<ULONG> references_{1};
    std::shared_ptr<Context> context_;
    ProviderIdentity identity_;
    int runtime_id_{};
    std::size_t index_;
    bool active_{true};
};

class RootProvider final : public IRawElementProviderSimple,
                           public IRawElementProviderFragment,
                           public IRawElementProviderFragmentRoot {
public:
    explicit RootProvider(std::shared_ptr<Context> context)
        : context_{std::move(context)} {
        bool changed = false;
        const auto synchronized = synchronize_children(context_->layout, changed);
        if (FAILED(synchronized)) throw std::bad_alloc{};
    }
    ~RootProvider() {
        if (context_->root == this) context_->root = nullptr;
        for (auto* child : children_) child->Release();
    }
    NodeProvider* child(std::size_t index) const {
        return index < children_.size() ? children_[index] : nullptr;
    }
    NodeProvider* child(std::string_view id) const {
        const auto found = children_by_id_.find(id);
        return found == children_by_id_.end() ? nullptr : found->second;
    }
    std::size_t child_count() const { return children_.size(); }
    HRESULT synchronize_children(const ShellLayoutResult& layout,
                                 bool& structure_changed) noexcept {
        return automation_boundary([&]() -> HRESULT {
            structure_changed = false;
            const std::size_t required = layout.nodes.empty()
                ? 0 : layout.nodes.size() - 1;
            if (required == children_.size()) {
                bool identical = true;
                for (std::size_t index = 0; index < required; ++index) {
                    if (!children_[index]->matches_identity(
                            layout.nodes[index + 1])) {
                        identical = false;
                        break;
                    }
                }
                if (identical) return S_OK;
            }

            std::vector<NodeProvider*> next;
            std::unordered_map<std::string_view, NodeProvider*> next_by_id;
            std::vector<std::unique_ptr<NodeProvider>> created;
            next.reserve(required);
            next_by_id.reserve(required);
            created.reserve(required);
            for (std::size_t index = 0; index < required; ++index) {
                const auto& node = layout.nodes[index + 1];
                if (next_by_id.contains(node.id)) return E_INVALIDARG;
                NodeProvider* provider = child(node.id);
                if (!provider || !provider->matches_identity(node)) {
#if defined(KF2_AUTOMATION_PROVIDER_TESTING)
                    if (context_->fail_next_child_allocation) {
                        context_->fail_next_child_allocation = false;
                        throw std::bad_alloc{};
                    }
#endif
                    if (next_runtime_id_ == std::numeric_limits<int>::max()) {
                        return E_OUTOFMEMORY;
                    }
                    auto added = std::make_unique<NodeProvider>(
                        context_, node, next_runtime_id_++, index);
                    provider = added.get();
                    created.push_back(std::move(added));
                }
                next.push_back(provider);
                next_by_id.emplace(provider->id(), provider);
            }

            for (auto* previous : children_) {
                const auto retained = next_by_id.find(previous->id());
                if (retained == next_by_id.end() ||
                    retained->second != previous) {
                    previous->deactivate();
                    previous->Release();
                }
            }
            for (std::size_t index = 0; index < next.size(); ++index) {
                next[index]->set_index(index);
            }
            children_ = std::move(next);
            children_by_id_ = std::move(next_by_id);
            for (auto& added : created) static_cast<void>(added.release());
            structure_changed = true;
            return S_OK;
        });
    }
    void raise_structure_changed() noexcept {
        static_cast<void>(UiaRaiseStructureChangedEvent(
            static_cast<IRawElementProviderSimple*>(this),
            StructureChangeType_ChildrenInvalidated, nullptr, 0));
    }

    IFACEMETHODIMP QueryInterface(REFIID id, void** object) override;
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG remaining = --references_;
        if (remaining == 0) delete this;
        return remaining;
    }
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions* options) override {
        if (!options) return E_POINTER;
        *options = kStaProviderOptions;
        return S_OK;
    }
    IFACEMETHODIMP GetPatternProvider(PATTERNID, IUnknown** provider) override {
        if (!provider) return E_POINTER;
        *provider = nullptr;
        return S_OK;
    }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID property, VARIANT* value) override {
        if (!value) return E_POINTER;
        VariantInit(value);
        if (property == UIA_NamePropertyId) return string_value(value, L"KF2 Optimizer Next");
        if (property == UIA_ControlTypePropertyId) return integer_value(value, UIA_WindowControlTypeId);
        return S_OK;
    }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** provider) override {
        if (!provider) return E_POINTER;
        *provider = nullptr;
        return UiaHostProviderFromHwnd(context_->window, provider);
    }
    IFACEMETHODIMP Navigate(NavigateDirection direction,
                            IRawElementProviderFragment** provider) override {
        if (!provider) return E_POINTER;
        *provider = nullptr;
        if (children_.empty()) return S_OK;
        if (direction == NavigateDirection_FirstChild) *provider = children_.front();
        if (direction == NavigateDirection_LastChild) {
            *provider = children_.back();
        }
        if (*provider) (*provider)->AddRef();
        return S_OK;
    }
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** runtime_id) override {
        if (!runtime_id) return E_POINTER;
        *runtime_id = nullptr;
        return S_OK;
    }
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* bounds) override {
        if (!bounds) return E_POINTER;
        *bounds = {};
        *bounds = screen_bounds(context_->window, context_->layout.root);
        return S_OK;
    }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** roots) override {
        if (!roots) return E_POINTER;
        *roots = nullptr;
        return S_OK;
    }
    IFACEMETHODIMP SetFocus() override { ::SetFocus(context_->window); return S_OK; }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** root) override {
        if (!root) return E_POINTER;
        *root = this;
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP ElementProviderFromPoint(double x, double y,
                                            IRawElementProviderFragment** provider) override;
    IFACEMETHODIMP GetFocus(IRawElementProviderFragment** provider) override;

private:
    std::atomic<ULONG> references_{1};
    std::shared_ptr<Context> context_;
    std::vector<NodeProvider*> children_;
    std::unordered_map<std::string_view, NodeProvider*> children_by_id_;
    int next_runtime_id_{1};
};

HRESULT NodeProvider::QueryInterface(REFIID id, void** object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (id == IID_IUnknown || id == IID_IRawElementProviderSimple) {
        *object = static_cast<IRawElementProviderSimple*>(this);
    } else if (id == IID_IRawElementProviderFragment) {
        *object = static_cast<IRawElementProviderFragment*>(this);
    } else if (id == IID_IInvokeProvider && identity_.supports_invoke()) {
        *object = static_cast<IInvokeProvider*>(this);
    } else if (id == IID_ISelectionItemProvider && identity_.destination) {
        *object = static_cast<ISelectionItemProvider*>(this);
    } else if (id == IID_IRangeValueProvider && identity_.slider) {
        *object = static_cast<IRangeValueProvider*>(this);
    } else return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

HRESULT NodeProvider::GetPatternProvider(PATTERNID pattern, IUnknown** provider) {
    if (!provider) return E_POINTER;
    *provider = nullptr;
    if (!node()) return UIA_E_ELEMENTNOTAVAILABLE;
    if (pattern == UIA_InvokePatternId && identity_.supports_invoke()) {
        *provider = static_cast<IInvokeProvider*>(this);
    }
    if (pattern == UIA_SelectionItemPatternId && identity_.destination) {
        *provider = static_cast<ISelectionItemProvider*>(this);
    }
    if (pattern == UIA_RangeValuePatternId && identity_.slider) {
        *provider = static_cast<IRangeValueProvider*>(this);
    }
    if (*provider) AddRef();
    return S_OK;
}

HRESULT NodeProvider::GetPropertyValue(PROPERTYID property, VARIANT* value) {
    if (!value) return E_POINTER;
    VariantInit(value);
    const auto* current = node();
    if (!current) return UIA_E_ELEMENTNOTAVAILABLE;
    if (property == UIA_NamePropertyId) return string_value(value, current->text);
    if (property == UIA_ControlTypePropertyId) {
        return integer_value(value, current->destination
                                        ? UIA_ListItemControlTypeId
                                        : current->role == SemanticRole::slider
                                            ? UIA_SliderControlTypeId
                                        : current->action_id ? UIA_ButtonControlTypeId
                                                           : UIA_TextControlTypeId);
    }
    if (property == UIA_IsKeyboardFocusablePropertyId) {
        return boolean_value(value, current->destination.has_value() ||
                                        (current->action_id.has_value() && current->enabled));
    }
    if (property == UIA_HasKeyboardFocusPropertyId) return boolean_value(value, current->focused);
    if (property == UIA_IsEnabledPropertyId) return boolean_value(value, current->enabled);
    if (property == UIA_IsInvokePatternAvailablePropertyId) {
        return boolean_value(value, identity_.supports_invoke());
    }
    if (property == UIA_IsSelectionItemPatternAvailablePropertyId) return boolean_value(value, identity_.destination.has_value());
    if (property == UIA_IsRangeValuePatternAvailablePropertyId) {
        return boolean_value(value, identity_.slider);
    }
    return S_OK;
}

HRESULT NodeProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment** provider) {
    if (!provider) return E_POINTER;
    *provider = nullptr;
    if (!node() || !context_->root) return UIA_E_ELEMENTNOTAVAILABLE;
    if (direction == NavigateDirection_Parent) *provider = context_->root;
    if (direction == NavigateDirection_NextSibling) *provider = context_->root->child(index_ + 1);
    if (direction == NavigateDirection_PreviousSibling && index_ > 0) *provider = context_->root->child(index_ - 1);
    if (*provider) (*provider)->AddRef();
    return S_OK;
}

HRESULT NodeProvider::GetRuntimeId(SAFEARRAY** runtime_id) {
    if (!runtime_id) return E_POINTER;
    *runtime_id = nullptr;
    if (!node()) return UIA_E_ELEMENTNOTAVAILABLE;
    int values[] = {UiaAppendRuntimeId, runtime_id_};
    *runtime_id = SafeArrayCreateVector(VT_I4, 0, 2);
    if (!*runtime_id) return E_OUTOFMEMORY;
    for (LONG position = 0; position < 2; ++position) {
        const auto written = SafeArrayPutElement(
            *runtime_id, &position, &values[position]);
        if (FAILED(written)) {
            SafeArrayDestroy(*runtime_id);
            *runtime_id = nullptr;
            return written;
        }
    }
    return S_OK;
}

HRESULT NodeProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** root) {
    if (!root) return E_POINTER;
    *root = nullptr;
    if (!node() || !context_->root) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *root = context_->root;
    (*root)->AddRef();
    return S_OK;
}

HRESULT NodeProvider::get_SelectionContainer(IRawElementProviderSimple** container) {
    if (!container) return E_POINTER;
    *container = nullptr;
    const auto* current = node();
    if (!current || !context_->root) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (!identity_.destination) {
        return UIA_E_NOTSUPPORTED;
    }
    *container = static_cast<IRawElementProviderSimple*>(context_->root);
    (*container)->AddRef();
    return S_OK;
}

HRESULT NodeProvider::SetValue(double requested) {
    return automation_boundary([&]() -> HRESULT {
        const auto* resolved = node();
        if (!resolved || !context_->model) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        const auto current = *resolved;
        if (current.role != SemanticRole::slider || !current.slider ||
            !current.action_id || !current.enabled) {
            return UIA_E_ELEMENTNOTENABLED;
        }
        if (!std::isfinite(requested) || !context_->set_slider_value) {
            return UIA_E_NOTSUPPORTED;
        }
        const int step = std::max(1, current.slider->small_step);
        int value = current.slider->minimum + static_cast<int>(std::lround(
            (requested - static_cast<double>(current.slider->minimum)) /
            static_cast<double>(step))) * step;
        value = std::clamp(value, current.slider->minimum,
                           current.slider->maximum);
        (void)context_->model->focus_action(*current.action_id);
        context_->set_slider_value(*current.action_id, value);
        if (context_->invalidate) context_->invalidate();
        return S_OK;
    });
}

HRESULT NodeProvider::get_Value(double* value) {
    if (!value) return E_POINTER;
    *value = 0.0;
    const auto* current = node();
    if (!current) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!current->slider) return UIA_E_NOTSUPPORTED;
    *value = current->slider->value;
    return S_OK;
}

HRESULT NodeProvider::get_IsReadOnly(BOOL* read_only) {
    if (!read_only) return E_POINTER;
    *read_only = TRUE;
    const auto* current = node();
    if (!current) return UIA_E_ELEMENTNOTAVAILABLE;
    *read_only = current->enabled ? FALSE : TRUE;
    return current->slider ? S_OK : UIA_E_NOTSUPPORTED;
}

HRESULT NodeProvider::get_Maximum(double* maximum) {
    if (!maximum) return E_POINTER;
    *maximum = 0.0;
    const auto* current = node();
    if (!current) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!current->slider) return UIA_E_NOTSUPPORTED;
    *maximum = current->slider->maximum;
    return S_OK;
}

HRESULT NodeProvider::get_Minimum(double* minimum) {
    if (!minimum) return E_POINTER;
    *minimum = 0.0;
    const auto* current = node();
    if (!current) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!current->slider) return UIA_E_NOTSUPPORTED;
    *minimum = current->slider->minimum;
    return S_OK;
}

HRESULT NodeProvider::get_LargeChange(double* change) {
    if (!change) return E_POINTER;
    *change = 0.0;
    const auto* current = node();
    if (!current) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!current->slider) return UIA_E_NOTSUPPORTED;
    *change = current->slider->large_step;
    return S_OK;
}

HRESULT NodeProvider::get_SmallChange(double* change) {
    if (!change) return E_POINTER;
    *change = 0.0;
    const auto* current = node();
    if (!current) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!current->slider) return UIA_E_NOTSUPPORTED;
    *change = current->slider->small_step;
    return S_OK;
}

HRESULT RootProvider::QueryInterface(REFIID id, void** object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (id == IID_IUnknown || id == IID_IRawElementProviderSimple) {
        *object = static_cast<IRawElementProviderSimple*>(this);
    } else if (id == IID_IRawElementProviderFragment) {
        *object = static_cast<IRawElementProviderFragment*>(this);
    } else if (id == IID_IRawElementProviderFragmentRoot) {
        *object = static_cast<IRawElementProviderFragmentRoot*>(this);
    } else return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

HRESULT RootProvider::ElementProviderFromPoint(double x, double y,
                                                IRawElementProviderFragment** provider) {
    if (!provider) return E_POINTER;
    *provider = nullptr;
    POINT origin{};
    ClientToScreen(context_->window, &origin);
    const float scale = static_cast<float>(GetDpiForWindow(context_->window)) / 96.0F;
    const DipPoint point{static_cast<float>(x - origin.x) / scale,
                         static_cast<float>(y - origin.y) / scale};
    const auto* found = hit_test(context_->layout, point);
    if (!found || found->role == SemanticRole::root) return S_OK;
    *provider = child(found->id);
    if (*provider) (*provider)->AddRef();
    return S_OK;
}

HRESULT RootProvider::GetFocus(IRawElementProviderFragment** provider) {
    if (!provider) return E_POINTER;
    *provider = nullptr;
    for (const auto& node : context_->layout.nodes) {
        if (node.role != SemanticRole::root && node.focused) {
            *provider = child(node.id);
            if (!*provider) return UIA_E_ELEMENTNOTAVAILABLE;
            (*provider)->AddRef();
            break;
        }
    }
    return S_OK;
}

}  // namespace

struct AutomationProvider::Impl {
    std::shared_ptr<Context> context;
    RootProvider* root{};
    void disconnect() noexcept {
        if (!root) return;
        static_cast<void>(UiaDisconnectProvider(
            static_cast<IRawElementProviderSimple*>(root)));
        context->connected = false;
        context->model = nullptr;
        context->activate_action = {};
        context->invalidate = {};
        context->set_slider_value = {};
        root->Release();
        root = nullptr;
    }
    ~Impl() { disconnect(); }
};

AutomationProvider::AutomationProvider(std::unique_ptr<Impl> implementation)
    : implementation_{std::move(implementation)} {}
AutomationProvider::AutomationProvider(AutomationProvider&&) noexcept = default;
AutomationProvider& AutomationProvider::operator=(AutomationProvider&&) noexcept = default;
AutomationProvider::~AutomationProvider() = default;

Result<AutomationProvider> AutomationProvider::create(HWND window, UiModel& model,
                                                       ShellLayoutResult layout,
                                                       std::function<void(std::string_view)> activate_action,
                                                       std::function<void()> invalidate,
                                                       std::function<void(std::string_view, int)>
                                                           set_slider_value) {
    if (!window || !IsWindow(window)) {
        return Result<AutomationProvider>::failure(
            {ErrorCode::invalid_argument, L"Automation requires a valid window", 0});
    }
    if (!current_thread_owns_sta()) {
        return Result<AutomationProvider>::failure({
            ErrorCode::platform_failure,
            L"Automation requires its owning UI thread to use a COM STA",
            static_cast<std::uint32_t>(RPC_E_WRONG_THREAD)});
    }
    try {
        auto implementation = std::make_unique<Impl>();
        implementation->context = std::make_shared<Context>();
        implementation->context->window = window;
        implementation->context->model = &model;
        implementation->context->layout = std::move(layout);
        implementation->context->activate_action = std::move(activate_action);
        implementation->context->invalidate = std::move(invalidate);
        implementation->context->set_slider_value =
            std::move(set_slider_value);
        implementation->root = new RootProvider(implementation->context);
        implementation->context->root = implementation->root;
        return Result<AutomationProvider>::success(
            AutomationProvider{std::move(implementation)});
    } catch (const std::bad_alloc&) {
        return Result<AutomationProvider>::failure({
            ErrorCode::internal_failure,
            L"Automation provider allocation failed", ERROR_NOT_ENOUGH_MEMORY});
    } catch (...) {
        return Result<AutomationProvider>::failure({
            ErrorCode::internal_failure,
            L"Automation provider initialization failed", 0});
    }
}

LRESULT AutomationProvider::handle_get_object(WPARAM wparam, LPARAM lparam) noexcept {
    if (static_cast<LONG>(lparam) != UiaRootObjectId) return 0;
    return UiaReturnRawElementProvider(
        implementation_->context->window, wparam, lparam,
        static_cast<IRawElementProviderSimple*>(implementation_->root));
}

bool AutomationProvider::update_layout(ShellLayoutResult layout) noexcept {
    if (!implementation_->context->connected) return false;
    try {
        bool structure_changed = false;
        if (FAILED(implementation_->root->synchronize_children(
                layout, structure_changed))) {
            return false;
        }
        implementation_->context->layout = std::move(layout);
        if (structure_changed) {
            implementation_->root->raise_structure_changed();
        }
        return true;
    } catch (...) {
        return false;
    }
}

#if defined(KF2_AUTOMATION_PROVIDER_TESTING)
void AutomationProvider::fail_next_child_allocation_for_testing() noexcept {
    implementation_->context->fail_next_child_allocation = true;
}

IUnknown* AutomationProvider::retain_child_for_testing(
    std::size_t index) const noexcept {
    if (!implementation_->root) return nullptr;
    auto* child = implementation_->root->child(index);
    if (child) child->AddRef();
    return static_cast<IRawElementProviderFragment*>(child);
}

void AutomationProvider::disconnect_for_testing() noexcept {
    implementation_->disconnect();
}

std::uint32_t AutomationProvider::provider_options_for_testing(
    bool child) const noexcept {
    ProviderOptions options{};
    IRawElementProviderSimple* provider =
        static_cast<IRawElementProviderSimple*>(implementation_->root);
    if (child) {
        provider = static_cast<IRawElementProviderSimple*>(
            implementation_->root->child(0));
    }
    return provider && SUCCEEDED(provider->get_ProviderOptions(&options))
        ? static_cast<std::uint32_t>(options)
        : 0U;
}
#endif

}  // namespace kf2::ui
