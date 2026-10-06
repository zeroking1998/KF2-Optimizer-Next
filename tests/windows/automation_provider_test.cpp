#include <Windows.h>
#include <ole2.h>
#include <UIAutomationCore.h>
#include <UIAutomationClient.h>
#include <UIAutomationCoreApi.h>
#include <wrl/client.h>

#include <cstdlib>
#include <atomic>
#include <chrono>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>
#include <utility>

#include "kf2/platform/windows/window.hpp"
#include "kf2/platform/windows/window_events.hpp"
#include "kf2/ui/automation_provider.hpp"
#include "kf2/ui/shell_layout.hpp"
#include "kf2/ui/ui_model.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

using Microsoft::WRL::ComPtr;

int runtime_suffix(IUIAutomationElement* element) {
    SAFEARRAY* runtime_id = nullptr;
    if (!element || FAILED(element->GetRuntimeId(&runtime_id)) || !runtime_id) {
        return std::numeric_limits<int>::min();
    }
    LONG upper = -1;
    int value = std::numeric_limits<int>::min();
    if (SUCCEEDED(SafeArrayGetUBound(runtime_id, 1, &upper)) && upper >= 0) {
        static_cast<void>(SafeArrayGetElement(runtime_id, &upper, &value));
    }
    SafeArrayDestroy(runtime_id);
    return value;
}

bool direct_runtime_id(IRawElementProviderFragment* fragment, int& suffix) {
    SAFEARRAY* runtime_id = nullptr;
    const auto result = fragment->GetRuntimeId(&runtime_id);
    if (FAILED(result) || !runtime_id) {
        if (runtime_id) SafeArrayDestroy(runtime_id);
        return false;
    }
    LONG lower = -1;
    LONG upper = -1;
    VARTYPE type = VT_EMPTY;
    LONG position = 0;
    int prefix = 0;
    bool valid = SafeArrayGetDim(runtime_id) == 1 &&
        SUCCEEDED(SafeArrayGetVartype(runtime_id, &type)) && type == VT_I4 &&
        SUCCEEDED(SafeArrayGetLBound(runtime_id, 1, &lower)) && lower == 0 &&
        SUCCEEDED(SafeArrayGetUBound(runtime_id, 1, &upper)) && upper == 1 &&
        SUCCEEDED(SafeArrayGetElement(runtime_id, &position, &prefix)) &&
        prefix == UiaAppendRuntimeId;
    position = 1;
    valid = valid &&
        SUCCEEDED(SafeArrayGetElement(runtime_id, &position, &suffix)) &&
        suffix > 0;
    return SUCCEEDED(SafeArrayDestroy(runtime_id)) && valid;
}

class AutomationSink final : public kf2::platform::windows::WindowEventSink {
public:
    void on_paint() override {}
    void on_resize(kf2::platform::windows::WindowSize) override {}
    void on_dpi_changed(kf2::platform::windows::DpiChangedEvent) override {}
    void on_key(kf2::platform::windows::KeyEvent) override {}
    void on_pointer(kf2::platform::windows::PointerEvent) override {}
    void on_theme_changed(kf2::platform::windows::ThemeChangedEvent) override {}
    bool on_close() override { return true; }
    LRESULT on_get_object(WPARAM wparam, LPARAM lparam) override {
        return provider == nullptr ? 0 : provider->handle_get_object(wparam, lparam);
    }
    kf2::ui::AutomationProvider* provider{nullptr};
};

int main() {
    CHECK(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
    AutomationSink sink;
    const auto window = kf2::platform::windows::Window::create(
        {.title = L"KF2 automation test", .width = 800, .height = 520,
         .visible = false, .sink = &sink});
    CHECK(window.has_value());
    const auto hwnd = static_cast<HWND>(window.value().native_handle_for_testing());

    kf2::ui::UiModel model;
    model.set_state_path(L"C:\\KF2Optimizer\\Data");
    std::string invoked_action;
    std::string changed_slider;
    int changed_slider_value = -1;
    const DWORD owning_thread = GetCurrentThreadId();
    std::atomic_bool callback_left_owning_thread{false};
    enum class CallbackFailure { none, activation, invalidation, slider };
    CallbackFailure callback_failure = CallbackFailure::none;
    auto provider = kf2::ui::AutomationProvider::create(
        hwnd, model, kf2::ui::layout_shell(model, 800.0F, 520.0F),
        [&](std::string_view action) {
            if (GetCurrentThreadId() != owning_thread) {
                callback_left_owning_thread = true;
            }
            if (callback_failure == CallbackFailure::activation) {
                throw std::runtime_error{"activation failed"};
            }
            invoked_action.assign(action);
        },
        [&] {
            if (GetCurrentThreadId() != owning_thread) {
                callback_left_owning_thread = true;
            }
            if (callback_failure == CallbackFailure::invalidation) {
                throw std::bad_alloc{};
            }
        },
        [&](std::string_view id, int value) {
            if (GetCurrentThreadId() != owning_thread) {
                callback_left_owning_thread = true;
            }
            if (callback_failure == CallbackFailure::slider) {
                throw std::runtime_error{"slider failed"};
            }
            changed_slider.assign(id);
            changed_slider_value = value;
        });
    CHECK(provider.has_value());
    sink.provider = &provider.value();
    constexpr auto com_threading =
        static_cast<std::uint32_t>(ProviderOptions_UseComThreading);
    CHECK((provider.value().provider_options_for_testing(false) &
           com_threading) != 0);
    CHECK((provider.value().provider_options_for_testing(true) &
           com_threading) != 0);

    ComPtr<IUIAutomation> automation;
    CHECK(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr,
                                     CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&automation))));
    ComPtr<IUIAutomationElement> root;
    CHECK(SUCCEEDED(automation->ElementFromHandle(hwnd, &root)));
    BSTR root_name = nullptr;
    CHECK(SUCCEEDED(root->get_CurrentName(&root_name)));
    CHECK(std::wstring_view{root_name} == L"KF2 Optimizer Next");
    SysFreeString(root_name);

    VARIANT list_item_type{};
    list_item_type.vt = VT_I4;
    list_item_type.lVal = UIA_ListItemControlTypeId;
    ComPtr<IUIAutomationCondition> condition;
    CHECK(SUCCEEDED(automation->CreatePropertyCondition(
        UIA_ControlTypePropertyId, list_item_type, &condition)));
    ComPtr<IUIAutomationElementArray> navigation;
    CHECK(SUCCEEDED(root->FindAll(TreeScope_Children, condition.Get(), &navigation)));
    int count = 0;
    CHECK(SUCCEEDED(navigation->get_Length(&count)));
    CHECK(count == 6);

    constexpr const wchar_t* expected[] = {
        L"Home", L"Game graphics", L"Overlay", L"Advanced settings",
        L"Debug", L"Help & Repair"};
    for (int index = 0; index < count; ++index) {
        ComPtr<IUIAutomationElement> item;
        CHECK(SUCCEEDED(navigation->GetElement(index, &item)));
        BSTR name = nullptr;
        CHECK(SUCCEEDED(item->get_CurrentName(&name)));
        CHECK(std::wstring_view{name} == expected[index]);
        SysFreeString(name);
        BOOL focusable = FALSE;
        CHECK(SUCCEEDED(item->get_CurrentIsKeyboardFocusable(&focusable)));
        CHECK(focusable == TRUE);
    }

    ComPtr<IUIAutomationElement> home_item;
    CHECK(SUCCEEDED(navigation->GetElement(0, &home_item)));
    ComPtr<IUIAutomationInvokePattern> home_invoke;
    CHECK(SUCCEEDED(home_item->GetCurrentPatternAs(
        UIA_InvokePatternId, IID_PPV_ARGS(&home_invoke))));
    CHECK(SUCCEEDED(home_invoke->Invoke()));
    CHECK(model.selected() == kf2::ui::Destination::dashboard);
    auto status = model.status();
    status.mode = L"Adaptive / Automatic";
    status.game_detected = true;
    model.set_status(status);
    CHECK(provider.value().update_layout(
        kf2::ui::layout_shell(model, 800.0F, 520.0F)));

    VARIANT target_name{};
    target_name.vt = VT_BSTR;
    target_name.bstrVal = SysAllocString(L"Target FPS");
    CHECK(target_name.bstrVal != nullptr);
    ComPtr<IUIAutomationCondition> target_condition;
    CHECK(SUCCEEDED(automation->CreatePropertyCondition(
        UIA_NamePropertyId, target_name, &target_condition)));
    VariantClear(&target_name);
    ComPtr<IUIAutomationElement> target_slider;
    CHECK(SUCCEEDED(root->FindFirst(
        TreeScope_Children, target_condition.Get(), &target_slider)));
    CHECK(target_slider != nullptr);
    CONTROLTYPEID target_type = 0;
    CHECK(SUCCEEDED(target_slider->get_CurrentControlType(&target_type)));
    CHECK(target_type == UIA_SliderControlTypeId);
    ComPtr<IUIAutomationRangeValuePattern> range;
    CHECK(SUCCEEDED(target_slider->GetCurrentPatternAs(
        UIA_RangeValuePatternId, IID_PPV_ARGS(&range))));
    double minimum = 0.0;
    double maximum = 0.0;
    CHECK(SUCCEEDED(range->get_CurrentMinimum(&minimum)));
    CHECK(SUCCEEDED(range->get_CurrentMaximum(&maximum)));
    CHECK(minimum == 30.0);
    CHECK(maximum == 240.0);
    CHECK(SUCCEEDED(range->SetValue(144.0)));
    CHECK(changed_slider == "settings-target-slider");
    CHECK(changed_slider_value == 144);

    // A client may request any double, not only values on the visual track.
    for (const auto& [requested, expected_value] : {
             std::pair{30.0, 30}, std::pair{240.0, 240},
             std::pair{144.49, 144}, std::pair{144.5, 145},
             std::pair{std::numeric_limits<double>::max(), 240},
             std::pair{-std::numeric_limits<double>::max(), 30}}) {
        CHECK(SUCCEEDED(range->SetValue(requested)));
        if (changed_slider_value != expected_value) {
            std::cerr << "Slider request " << requested << " returned "
                      << changed_slider_value << "; expected "
                      << expected_value << '\n';
        }
        CHECK(changed_slider_value == expected_value);
    }
    for (const double invalid : {
             std::numeric_limits<double>::quiet_NaN(),
             std::numeric_limits<double>::infinity(),
             -std::numeric_limits<double>::infinity()}) {
        const int unchanged_value = changed_slider_value;
        CHECK(range->SetValue(invalid) == UIA_E_NOTSUPPORTED);
        CHECK(changed_slider_value == unchanged_value);
    }

    // Exercise metadata through the real provider, without a second slider
    // implementation in the test. Provider identity stays unchanged.
    auto numeric_layout = kf2::ui::layout_shell(model, 800.0F, 520.0F);
    auto* numeric_slider = [&]() -> kf2::ui::SemanticNode* {
        for (auto& node : numeric_layout.nodes) {
            if (node.action_id == "settings-target-slider") return &node;
        }
        return nullptr;
    }();
    CHECK(numeric_slider != nullptr && numeric_slider->slider.has_value());
    for (const auto invalid : {
             kf2::ui::SliderInfo{30, 29, 30, 1},
             kf2::ui::SliderInfo{30, 240, 30, 0},
             kf2::ui::SliderInfo{30, 240, 30, -1}}) {
        numeric_slider->slider = invalid;
        CHECK(provider.value().update_layout(numeric_layout));
        const int unchanged_value = changed_slider_value;
        CHECK(range->SetValue(144.0) == UIA_E_NOTSUPPORTED);
        CHECK(changed_slider_value == unchanged_value);
    }
    numeric_slider->slider = kf2::ui::SliderInfo{10, 31, 10, 4};
    CHECK(provider.value().update_layout(numeric_layout));
    for (const auto& [requested, expected_value] : {
             std::pair{10.0, 10}, std::pair{31.0, 31},
             std::pair{11.99, 10}, std::pair{12.0, 14},
             std::pair{29.0, 30}, std::pair{30.5, 30}}) {
        CHECK(SUCCEEDED(range->SetValue(requested)));
        CHECK(changed_slider_value == expected_value);
    }
    numeric_slider->slider = kf2::ui::SliderInfo{
        std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), 0, 1};
    CHECK(provider.value().update_layout(numeric_layout));
    for (const auto& [requested, expected_value] : {
             std::pair{0.0, 0},
             std::pair{std::numeric_limits<double>::max(),
                       std::numeric_limits<int>::max()},
             std::pair{-std::numeric_limits<double>::max(),
                       std::numeric_limits<int>::min()}}) {
        CHECK(SUCCEEDED(range->SetValue(requested)));
        CHECK(changed_slider_value == expected_value);
    }
    numeric_slider->slider = kf2::ui::SliderInfo{42, 42, 42, 1};
    CHECK(provider.value().update_layout(numeric_layout));
    CHECK(SUCCEEDED(range->SetValue(std::numeric_limits<double>::max())));
    CHECK(changed_slider_value == 42);
    CHECK(provider.value().update_layout(
        kf2::ui::layout_shell(model, 800.0F, 520.0F)));
    CHECK(SUCCEEDED(range->SetValue(144.0)));

    callback_failure = CallbackFailure::slider;
    CHECK(range->SetValue(150.0) == E_FAIL);
    CHECK(changed_slider_value == 144);
    callback_failure = CallbackFailure::invalidation;
    CHECK(home_invoke->Invoke() == E_OUTOFMEMORY);
    callback_failure = CallbackFailure::none;

    VARIANT launch_name{};
    launch_name.vt = VT_BSTR;
    launch_name.bstrVal = SysAllocString(L"LAUNCH KF2");
    CHECK(launch_name.bstrVal != nullptr);
    ComPtr<IUIAutomationCondition> launch_condition;
    CHECK(SUCCEEDED(automation->CreatePropertyCondition(
        UIA_NamePropertyId, launch_name, &launch_condition)));
    VariantClear(&launch_name);
    ComPtr<IUIAutomationElement> launch_button;
    CHECK(SUCCEEDED(root->FindFirst(
        TreeScope_Children, launch_condition.Get(), &launch_button)));
    CHECK(launch_button != nullptr);
    ComPtr<IUIAutomationInvokePattern> launch_invoke;
    CHECK(SUCCEEDED(launch_button->GetCurrentPatternAs(
        UIA_InvokePatternId, IID_PPV_ARGS(&launch_invoke))));
    callback_failure = CallbackFailure::activation;
    CHECK(launch_invoke->Invoke() == E_FAIL);
    CHECK(invoked_action.empty());
    callback_failure = CallbackFailure::none;

    const int retained_launch_runtime = runtime_suffix(launch_button.Get());
    CHECK(retained_launch_runtime != std::numeric_limits<int>::min());
    model.set_notice({kf2::ui::NoticeSeverity::warning, L"TEST_NOTICE",
                      L"Retained provider identity test", L""});
    CHECK(provider.value().update_layout(
        kf2::ui::layout_shell(model, 800.0F, 520.0F)));
    BSTR retained_name = nullptr;
    CHECK(SUCCEEDED(launch_button->get_CurrentName(&retained_name)));
    CHECK(std::wstring_view{retained_name} == L"LAUNCH KF2");
    SysFreeString(retained_name);
    invoked_action.clear();
    CHECK(SUCCEEDED(launch_invoke->Invoke()));
    CHECK(invoked_action == "dashboard-launch");
    CHECK(runtime_suffix(launch_button.Get()) == retained_launch_runtime);

    CHECK(provider.value().update_layout(
        kf2::ui::layout_shell(model, 640.0F, 520.0F)));
    CHECK(runtime_suffix(launch_button.Get()) == retained_launch_runtime);
    model.set_scroll_extent(500.0F);
    static_cast<void>(model.set_scroll(120.0F));
    CHECK(provider.value().update_layout(
        kf2::ui::layout_shell(model, 640.0F, 520.0F)));
    CHECK(runtime_suffix(launch_button.Get()) == retained_launch_runtime);

    model.clear_notice();
    static_cast<void>(model.focus_destination(kf2::ui::Destination::graphics));
    static_cast<void>(model.activate_focused());
    CHECK(provider.value().update_layout(
        kf2::ui::layout_shell(model, 800.0F, 520.0F)));
    retained_name = nullptr;
    CHECK(launch_button->get_CurrentName(&retained_name) ==
          UIA_E_ELEMENTNOTAVAILABLE);
    SysFreeString(retained_name);
    CHECK(launch_invoke->Invoke() == UIA_E_ELEMENTNOTAVAILABLE);
    const int value_before_removal = changed_slider_value;
    const HRESULT removed_slider_result = range->SetValue(155.0);
    CHECK(FAILED(removed_slider_result));
    CHECK(changed_slider_value == value_before_removal);

    static_cast<void>(model.focus_destination(kf2::ui::Destination::dashboard));
    static_cast<void>(model.activate_focused());
    CHECK(provider.value().update_layout(
        kf2::ui::layout_shell(model, 800.0F, 520.0F)));
    ComPtr<IUIAutomationElement> replacement_launch;
    CHECK(SUCCEEDED(root->FindFirst(
        TreeScope_Children, launch_condition.Get(), &replacement_launch)));
    CHECK(replacement_launch != nullptr);
    CHECK(runtime_suffix(replacement_launch.Get()) != retained_launch_runtime);

    auto expanded_layout =
        kf2::ui::layout_shell(model, 800.0F, 520.0F);
    expanded_layout.nodes.push_back(expanded_layout.nodes.back());
    expanded_layout.nodes.back().id += "-allocation-failure";
    provider.value().fail_next_child_allocation_for_testing();
    CHECK(!provider.value().update_layout(std::move(expanded_layout)));
    CHECK(SUCCEEDED(home_invoke->Invoke()));

    std::atomic_bool mta_complete{false};
    std::atomic_bool mta_failed{false};
    std::thread mta_client{[&] {
        const HRESULT initialized =
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(initialized)) {
            mta_failed = true;
            mta_complete = true;
            return;
        }
        ComPtr<IUIAutomation> client;
        if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr,
                                    CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&client)))) {
            mta_failed = true;
        }
        VARIANT item_type{};
        item_type.vt = VT_I4;
        item_type.lVal = UIA_ListItemControlTypeId;
        ComPtr<IUIAutomationCondition> mta_condition;
        if (!mta_failed &&
            FAILED(client->CreatePropertyCondition(
                UIA_ControlTypePropertyId, item_type, &mta_condition))) {
            mta_failed = true;
        }
        for (int iteration = 0; iteration < 8 && !mta_failed; ++iteration) {
            ComPtr<IUIAutomationElement> queried_root;
            if (FAILED(client->ElementFromHandle(hwnd, &queried_root))) {
                mta_failed = true;
                break;
            }
            BSTR name = nullptr;
            if (FAILED(queried_root->get_CurrentName(&name)) ||
                std::wstring_view{name ? name : L""} !=
                    L"KF2 Optimizer Next") {
                mta_failed = true;
            }
            SysFreeString(name);
            ComPtr<IUIAutomationElementArray> children;
            if (FAILED(queried_root->FindAll(TreeScope_Children,
                                             mta_condition.Get(), &children))) {
                mta_failed = true;
                continue;
            }
            int child_count = 0;
            ComPtr<IUIAutomationElement> first_child;
            ComPtr<IUIAutomationInvokePattern> invoke;
            if (FAILED(children->get_Length(&child_count)) || child_count == 0 ||
                FAILED(children->GetElement(0, &first_child)) ||
                FAILED(first_child->GetCurrentPatternAs(
                    UIA_InvokePatternId, IID_PPV_ARGS(&invoke))) ||
                FAILED(invoke->Invoke())) {
                mta_failed = true;
            }
        }
        CoUninitialize();
        mta_complete = true;
    }};
    const auto stress_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{30};
    while (!mta_complete && std::chrono::steady_clock::now() < stress_deadline) {
        if (!provider.value().update_layout(
                kf2::ui::layout_shell(model, 800.0F, 520.0F))) {
            mta_failed = true;
        }
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    }
    if (!mta_complete) {
        std::cerr << __FILE__ << ':' << __LINE__
                  << ": MTA UI Automation client did not shut down\n";
        std::_Exit(EXIT_FAILURE);
    }
    mta_client.join();
    CHECK(!mta_failed);
    CHECK(!callback_left_owning_thread);

    std::atomic_bool mta_creation_failed_closed{false};
    std::thread mta_creator{[&] {
        const HRESULT initialized =
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(initialized)) {
            auto rejected = kf2::ui::AutomationProvider::create(
                hwnd, model, kf2::ui::layout_shell(model, 800.0F, 520.0F));
            mta_creation_failed_closed = !rejected.has_value();
            CoUninitialize();
        }
    }};
    mta_creator.join();
    CHECK(mta_creation_failed_closed);

    ComPtr<IUnknown> retained_child;
    retained_child.Attach(provider.value().retain_child_for_testing(0));
    CHECK(retained_child != nullptr);
    ComPtr<IRawElementProviderFragment> retained_fragment;
    CHECK(SUCCEEDED(retained_child.As(&retained_fragment)));
    CHECK(retained_fragment->GetRuntimeId(nullptr) == E_POINTER);
    int original_id = 0;
    CHECK(direct_runtime_id(retained_fragment.Get(), original_id));
    for (LONG position = 0; position < 2; ++position) {
        const HRESULT failure = position == 0 ? E_ACCESSDENIED : E_UNEXPECTED;
        provider.value().fail_runtime_id_write_for_testing(position, failure);
        // The provider must clear even a pre-populated caller output pointer.
        SAFEARRAY* failed_id = reinterpret_cast<SAFEARRAY*>(1);
        CHECK(retained_fragment->GetRuntimeId(&failed_id) == failure);
        CHECK(failed_id == nullptr);
        CHECK(provider.value().runtime_id_cleanup_result_for_testing() == S_OK);
        int retried_id = 0;
        CHECK(direct_runtime_id(retained_fragment.Get(), retried_id));
        CHECK(retried_id == original_id);
    }
    provider.value().disconnect_for_testing();
    SAFEARRAY* disconnected_id = reinterpret_cast<SAFEARRAY*>(1);
    CHECK(retained_fragment->GetRuntimeId(&disconnected_id) ==
          UIA_E_ELEMENTNOTAVAILABLE);
    CHECK(disconnected_id == nullptr);
    for (const auto direction : {NavigateDirection_NextSibling,
                                 NavigateDirection_PreviousSibling}) {
        IRawElementProviderFragment* sibling = retained_fragment.Get();
        CHECK(retained_fragment->Navigate(direction, &sibling) ==
              UIA_E_ELEMENTNOTAVAILABLE);
        CHECK(sibling == nullptr);
    }

    sink.provider = nullptr;
    CoUninitialize();
    return EXIT_SUCCESS;
}
