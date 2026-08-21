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
#include <memory>
#include <utility>
#include <vector>

namespace kf2::ui {
namespace {

struct Context;
class RootProvider;

HRESULT string_value(VARIANT* value, const std::wstring& text) {
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
};

class NodeProvider final : public IRawElementProviderSimple,
                           public IRawElementProviderFragment,
                           public IInvokeProvider,
                           public ISelectionItemProvider,
                           public IRangeValueProvider {
public:
    NodeProvider(std::shared_ptr<Context> context, std::size_t index)
        : context_{std::move(context)}, index_{index} {}

    IFACEMETHODIMP QueryInterface(REFIID id, void** object) override;
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG remaining = --references_;
        if (remaining == 0) delete this;
        return remaining;
    }
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions* options) override {
        if (!options) return E_POINTER;
        *options = ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    IFACEMETHODIMP GetPatternProvider(PATTERNID pattern, IUnknown** provider) override;
    IFACEMETHODIMP GetPropertyValue(PROPERTYID property, VARIANT* value) override;
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** provider) override {
        if (!provider) return E_POINTER;
        *provider = nullptr;
        return S_OK;
    }
    IFACEMETHODIMP Navigate(NavigateDirection direction,
                            IRawElementProviderFragment** provider) override;
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** runtime_id) override;
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* bounds) override {
        if (!bounds) return E_POINTER;
        *bounds = screen_bounds(context_->window, node().bounds);
        return S_OK;
    }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** roots) override {
        if (!roots) return E_POINTER;
        *roots = nullptr;
        return S_OK;
    }
    IFACEMETHODIMP SetFocus() override {
        if (!context_->connected || !context_->model) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        const auto& current = node();
        if (current.destination) {
            (void)context_->model->focus_destination(*current.destination);
        } else if (current.action_id && current.enabled) {
            (void)context_->model->focus_action(*current.action_id);
        } else {
            return UIA_E_NOTSUPPORTED;
        }
        ::SetFocus(context_->window);
        if (context_->invalidate) context_->invalidate();
        return S_OK;
    }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** root) override;
    IFACEMETHODIMP Invoke() override {
        if (!context_->connected || !context_->model) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        const auto current = node();
        if (current.destination) {
            (void)context_->model->focus_destination(*current.destination);
            (void)context_->model->activate_focused();
            if (context_->invalidate) context_->invalidate();
            return S_OK;
        }
        if (!current.action_id || !current.enabled || !context_->activate_action) {
            return UIA_E_NOTSUPPORTED;
        }
        (void)context_->model->focus_action(*current.action_id);
        if (context_->invalidate) context_->invalidate();
        context_->activate_action(*current.action_id);
        return S_OK;
    }
    IFACEMETHODIMP Select() override {
        return node().destination ? Invoke() : UIA_E_NOTSUPPORTED;
    }
    IFACEMETHODIMP AddToSelection() override { return UIA_E_INVALIDOPERATION; }
    IFACEMETHODIMP RemoveFromSelection() override { return UIA_E_INVALIDOPERATION; }
    IFACEMETHODIMP get_IsSelected(BOOL* selected) override {
        if (!selected) return E_POINTER;
        *selected = node().selected ? TRUE : FALSE;
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

private:
    const SemanticNode& node() const {
        static const SemanticNode unavailable{
            "unavailable", SemanticRole::status, {}, L"Unavailable", std::nullopt,
            false, false, false, std::nullopt};
        return index_ + 1 < context_->layout.nodes.size()
                   ? context_->layout.nodes[index_ + 1]
                   : unavailable;
    }
    std::atomic<ULONG> references_{1};
    std::shared_ptr<Context> context_;
    std::size_t index_;
};

class RootProvider final : public IRawElementProviderSimple,
                           public IRawElementProviderFragment,
                           public IRawElementProviderFragmentRoot {
public:
    explicit RootProvider(std::shared_ptr<Context> context)
        : context_{std::move(context)} {
        const std::size_t count = context_->layout.nodes.empty()
                                      ? 0
                                      : context_->layout.nodes.size() - 1;
        children_.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            children_.push_back(new NodeProvider(context_, index));
        }
    }
    ~RootProvider() {
        if (context_->root == this) context_->root = nullptr;
        for (auto* child : children_) child->Release();
    }
    NodeProvider* child(std::size_t index) const {
        return index < visible_child_count() ? children_[index] : nullptr;
    }
    std::size_t child_count() const { return children_.size(); }
    void synchronize_children() {
        const std::size_t required = context_->layout.nodes.empty()
                                         ? 0
                                         : context_->layout.nodes.size() - 1;
        while (children_.size() < required) {
            children_.push_back(new NodeProvider(context_, children_.size()));
        }
    }
    std::size_t visible_child_count() const {
        return context_->layout.nodes.empty()
                   ? 0
                   : std::min(children_.size(), context_->layout.nodes.size() - 1);
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
        *options = ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    IFACEMETHODIMP GetPatternProvider(PATTERNID, IUnknown** provider) override {
        if (!provider) return E_POINTER;
        *provider = nullptr;
        return S_OK;
    }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID property, VARIANT* value) override {
        if (!value) return E_POINTER;
        if (property == UIA_NamePropertyId) return string_value(value, L"KF2 Optimizer Next");
        if (property == UIA_ControlTypePropertyId) return integer_value(value, UIA_WindowControlTypeId);
        VariantInit(value);
        return S_OK;
    }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** provider) override {
        if (!provider) return E_POINTER;
        return UiaHostProviderFromHwnd(context_->window, provider);
    }
    IFACEMETHODIMP Navigate(NavigateDirection direction,
                            IRawElementProviderFragment** provider) override {
        if (!provider) return E_POINTER;
        *provider = nullptr;
        if (visible_child_count() == 0) return S_OK;
        if (direction == NavigateDirection_FirstChild) *provider = children_.front();
        if (direction == NavigateDirection_LastChild) {
            *provider = children_[visible_child_count() - 1];
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
};

HRESULT NodeProvider::QueryInterface(REFIID id, void** object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (id == IID_IUnknown || id == IID_IRawElementProviderSimple) {
        *object = static_cast<IRawElementProviderSimple*>(this);
    } else if (id == IID_IRawElementProviderFragment) {
        *object = static_cast<IRawElementProviderFragment*>(this);
    } else if (id == IID_IInvokeProvider &&
               (node().destination ||
                (node().role == SemanticRole::action && node().action_id &&
                 node().enabled))) {
        *object = static_cast<IInvokeProvider*>(this);
    } else if (id == IID_ISelectionItemProvider && node().destination) {
        *object = static_cast<ISelectionItemProvider*>(this);
    } else if (id == IID_IRangeValueProvider &&
               node().role == SemanticRole::slider && node().slider) {
        *object = static_cast<IRangeValueProvider*>(this);
    } else return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

HRESULT NodeProvider::GetPatternProvider(PATTERNID pattern, IUnknown** provider) {
    if (!provider) return E_POINTER;
    *provider = nullptr;
    if (pattern == UIA_InvokePatternId &&
        (node().destination ||
         (node().role == SemanticRole::action && node().action_id &&
          node().enabled))) {
        *provider = static_cast<IInvokeProvider*>(this);
    }
    if (pattern == UIA_SelectionItemPatternId && node().destination) {
        *provider = static_cast<ISelectionItemProvider*>(this);
    }
    if (pattern == UIA_RangeValuePatternId &&
        node().role == SemanticRole::slider && node().slider) {
        *provider = static_cast<IRangeValueProvider*>(this);
    }
    if (*provider) AddRef();
    return S_OK;
}

HRESULT NodeProvider::GetPropertyValue(PROPERTYID property, VARIANT* value) {
    if (!value) return E_POINTER;
    if (property == UIA_NamePropertyId) return string_value(value, node().text);
    if (property == UIA_ControlTypePropertyId) {
        return integer_value(value, node().destination
                                        ? UIA_ListItemControlTypeId
                                        : node().role == SemanticRole::slider
                                            ? UIA_SliderControlTypeId
                                        : node().action_id ? UIA_ButtonControlTypeId
                                                           : UIA_TextControlTypeId);
    }
    if (property == UIA_IsKeyboardFocusablePropertyId) {
        return boolean_value(value, node().destination.has_value() ||
                                        (node().action_id.has_value() && node().enabled));
    }
    if (property == UIA_HasKeyboardFocusPropertyId) return boolean_value(value, node().focused);
    if (property == UIA_IsEnabledPropertyId) return boolean_value(value, node().enabled);
    if (property == UIA_IsInvokePatternAvailablePropertyId) {
        return boolean_value(value, node().destination.has_value() ||
                                        (node().role == SemanticRole::action &&
                                         node().action_id.has_value() &&
                                         node().enabled));
    }
    if (property == UIA_IsSelectionItemPatternAvailablePropertyId) return boolean_value(value, node().destination.has_value());
    if (property == UIA_IsRangeValuePatternAvailablePropertyId) {
        return boolean_value(value, node().role == SemanticRole::slider &&
                                        node().slider.has_value());
    }
    VariantInit(value);
    return S_OK;
}

HRESULT NodeProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment** provider) {
    if (!provider) return E_POINTER;
    *provider = nullptr;
    if (direction == NavigateDirection_Parent) *provider = context_->root;
    if (direction == NavigateDirection_NextSibling) *provider = context_->root->child(index_ + 1);
    if (direction == NavigateDirection_PreviousSibling && index_ > 0) *provider = context_->root->child(index_ - 1);
    if (*provider) (*provider)->AddRef();
    return S_OK;
}

HRESULT NodeProvider::GetRuntimeId(SAFEARRAY** runtime_id) {
    if (!runtime_id) return E_POINTER;
    int values[] = {UiaAppendRuntimeId, static_cast<int>(index_ + 1)};
    *runtime_id = SafeArrayCreateVector(VT_I4, 0, 2);
    if (!*runtime_id) return E_OUTOFMEMORY;
    for (LONG position = 0; position < 2; ++position) SafeArrayPutElement(*runtime_id, &position, &values[position]);
    return S_OK;
}

HRESULT NodeProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** root) {
    if (!root) return E_POINTER;
    if (!context_->connected || !context_->root) {
        *root = nullptr;
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *root = context_->root;
    (*root)->AddRef();
    return S_OK;
}

HRESULT NodeProvider::get_SelectionContainer(IRawElementProviderSimple** container) {
    if (!container) return E_POINTER;
    if (!context_->connected || !context_->root) {
        *container = nullptr;
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (!node().destination) {
        *container = nullptr;
        return UIA_E_NOTSUPPORTED;
    }
    *container = static_cast<IRawElementProviderSimple*>(context_->root);
    (*container)->AddRef();
    return S_OK;
}

HRESULT NodeProvider::SetValue(double requested) {
    const auto current = node();
    if (!context_->connected || current.role != SemanticRole::slider ||
        !current.slider || !current.action_id || !current.enabled) {
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
}

HRESULT NodeProvider::get_Value(double* value) {
    if (!value) return E_POINTER;
    if (!node().slider) return UIA_E_NOTSUPPORTED;
    *value = node().slider->value;
    return S_OK;
}

HRESULT NodeProvider::get_IsReadOnly(BOOL* read_only) {
    if (!read_only) return E_POINTER;
    *read_only = node().enabled ? FALSE : TRUE;
    return node().slider ? S_OK : UIA_E_NOTSUPPORTED;
}

HRESULT NodeProvider::get_Maximum(double* maximum) {
    if (!maximum) return E_POINTER;
    if (!node().slider) return UIA_E_NOTSUPPORTED;
    *maximum = node().slider->maximum;
    return S_OK;
}

HRESULT NodeProvider::get_Minimum(double* minimum) {
    if (!minimum) return E_POINTER;
    if (!node().slider) return UIA_E_NOTSUPPORTED;
    *minimum = node().slider->minimum;
    return S_OK;
}

HRESULT NodeProvider::get_LargeChange(double* change) {
    if (!change) return E_POINTER;
    if (!node().slider) return UIA_E_NOTSUPPORTED;
    *change = node().slider->large_step;
    return S_OK;
}

HRESULT NodeProvider::get_SmallChange(double* change) {
    if (!change) return E_POINTER;
    if (!node().slider) return UIA_E_NOTSUPPORTED;
    *change = node().slider->small_step;
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
    const auto index = static_cast<std::size_t>(found - context_->layout.nodes.data() - 1);
    *provider = child(index);
    if (*provider) (*provider)->AddRef();
    return S_OK;
}

HRESULT RootProvider::GetFocus(IRawElementProviderFragment** provider) {
    if (!provider) return E_POINTER;
    *provider = nullptr;
    for (std::size_t index = 0; index < visible_child_count(); ++index) {
        if (context_->layout.nodes[index + 1].focused) {
            *provider = child(index);
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
    ~Impl() {
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
    return Result<AutomationProvider>::success(AutomationProvider{std::move(implementation)});
}

LRESULT AutomationProvider::handle_get_object(WPARAM wparam, LPARAM lparam) noexcept {
    if (static_cast<LONG>(lparam) != UiaRootObjectId) return 0;
    return UiaReturnRawElementProvider(
        implementation_->context->window, wparam, lparam,
        static_cast<IRawElementProviderSimple*>(implementation_->root));
}

void AutomationProvider::update_layout(ShellLayoutResult layout) {
    if (!implementation_->context->connected) return;
    implementation_->context->layout = std::move(layout);
    implementation_->root->synchronize_children();
}

}  // namespace kf2::ui
