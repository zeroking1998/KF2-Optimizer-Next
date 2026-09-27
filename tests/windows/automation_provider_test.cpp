#include <Windows.h>
#include <ole2.h>
#include <UIAutomationCore.h>
#include <UIAutomationClient.h>
#include <wrl/client.h>

#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

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
    enum class CallbackFailure { none, activation, invalidation, slider };
    CallbackFailure callback_failure = CallbackFailure::none;
    auto provider = kf2::ui::AutomationProvider::create(
        hwnd, model, kf2::ui::layout_shell(model, 800.0F, 520.0F),
        [&](std::string_view action) {
            if (callback_failure == CallbackFailure::activation) {
                throw std::runtime_error{"activation failed"};
            }
            invoked_action.assign(action);
        },
        [&] {
            if (callback_failure == CallbackFailure::invalidation) {
                throw std::bad_alloc{};
            }
        },
        [&](std::string_view id, int value) {
            if (callback_failure == CallbackFailure::slider) {
                throw std::runtime_error{"slider failed"};
            }
            changed_slider.assign(id);
            changed_slider_value = value;
        });
    CHECK(provider.has_value());
    sink.provider = &provider.value();

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

    auto expanded_layout =
        kf2::ui::layout_shell(model, 800.0F, 520.0F);
    expanded_layout.nodes.push_back(expanded_layout.nodes.back());
    expanded_layout.nodes.back().id += "-allocation-failure";
    provider.value().fail_next_child_allocation_for_testing();
    CHECK(!provider.value().update_layout(std::move(expanded_layout)));
    CHECK(SUCCEEDED(home_invoke->Invoke()));

    sink.provider = nullptr;
    CoUninitialize();
    return EXIT_SUCCESS;
}
