#include "uia_identity.h"

#include <windows.h>

#include <objbase.h>
#include <uiautomationclient.h>

#include <cstring>
#include <functional>
#include <optional>
#include <string>

// CLSID_CUIAutomation / IID_IUIAutomation as real definitions (not DEFINE_GUID,
// which without INITGUID is only an external reference and would fail to link).
// We never link uiautomationcore.lib — the DLL is resolved by CoCreateInstance.
static const CLSID kClsidCUIAutomation = {
    0xFF48DBA4, 0x60EF, 0x4201,
    {0xAA, 0x87, 0x54, 0x10, 0x3E, 0xEF, 0x59, 0x4E}};
static const IID kIidIUIAutomation = {
    0x50A2FA23, 0xAEA1, 0x465B,
    {0x98, 0x47, 0x25, 0x06, 0x5D, 0x67, 0xFE, 0x2F}};

namespace {

// FNV-1a 64-bit, used to fold a UIA runtime id (array of ints) into a compact,
// stable element hash. Runtime ids are process-unique identifiers that persist
// for the lifetime of the element, so they are the recommended stable identity.
uint64_t hashRuntimeId(const SAFEARRAY* sa) {
    uint64_t h = 1469598103934665603ULL;  // FNV offset basis
    if (!sa) return h;
    auto* arr = const_cast<SAFEARRAY*>(sa);
    long lo = 0, hi = 0;
    SafeArrayGetLBound(arr, 1, &lo);
    SafeArrayGetUBound(arr, 1, &hi);
    for (long i = lo; i <= hi; ++i) {
        LONG v = 0;
        SafeArrayGetElement(arr, &i, &v);
        auto bytes = reinterpret_cast<const unsigned char*>(&v);
        for (int b = 0; b < static_cast<int>(sizeof(v)); ++b) {
            h ^= bytes[b];
            h *= 1099511628211ULL;  // FNV prime
        }
    }
    return h;
}

// Best-effort mapping of a UIA control-type id to a human label. Only the common
// ones are named; anything else is rendered as its numeric id.
std::wstring controlTypeLabel(CONTROLTYPEID id) {
    switch (id) {
        case UIA_ButtonControlTypeId: return L"Button";
        case UIA_CalendarControlTypeId: return L"Calendar";
        case UIA_CheckBoxControlTypeId: return L"CheckBox";
        case UIA_ComboBoxControlTypeId: return L"ComboBox";
        case UIA_EditControlTypeId: return L"Edit";
        case UIA_HyperlinkControlTypeId: return L"Hyperlink";
        case UIA_ImageControlTypeId: return L"Image";
        case UIA_ListControlTypeId: return L"List";
        case UIA_ListItemControlTypeId: return L"ListItem";
        case UIA_MenuControlTypeId: return L"Menu";
        case UIA_MenuItemControlTypeId: return L"MenuItem";
        case UIA_ProgressBarControlTypeId: return L"ProgressBar";
        case UIA_RadioButtonControlTypeId: return L"RadioButton";
        case UIA_ScrollBarControlTypeId: return L"ScrollBar";
        case UIA_SliderControlTypeId: return L"Slider";
        case UIA_SpinnerControlTypeId: return L"Spinner";
        case UIA_StatusBarControlTypeId: return L"StatusBar";
        case UIA_TabControlTypeId: return L"Tab";
        case UIA_TabItemControlTypeId: return L"TabItem";
        case UIA_TextControlTypeId: return L"Text";
        case UIA_ToolBarControlTypeId: return L"ToolBar";
        case UIA_ToolTipControlTypeId: return L"ToolTip";
        case UIA_TreeControlTypeId: return L"Tree";
        case UIA_TreeItemControlTypeId: return L"TreeItem";
        case UIA_WindowControlTypeId: return L"Window";
        case UIA_DocumentControlTypeId: return L"Document";
        case UIA_GroupControlTypeId: return L"Group";
        case UIA_PaneControlTypeId: return L"Pane";
        case UIA_HeaderControlTypeId: return L"Header";
        case UIA_HeaderItemControlTypeId: return L"HeaderItem";
        case UIA_TableControlTypeId: return L"Table";
        case UIA_TitleBarControlTypeId: return L"TitleBar";
        case UIA_SeparatorControlTypeId: return L"Separator";
        default: {
            wchar_t buf[32];
            wsprintfW(buf, L"Type#%u", static_cast<unsigned>(id));
            return buf;
        }
    }
}

// The HWND of the top-level window owning the element under the point. Used both
// as fallback identity and to support the architecture's "avoid capturing from
// protected/secure surfaces" rule.
HWND ownerWindowAt(POINT point) {
    HWND hit = WindowFromPoint(point);
    if (!hit) return nullptr;
    // Walk up to the nearest top-level owned window.
    HWND root = GetAncestor(hit, GA_ROOT);
    return root ? root : hit;
}

}  // namespace

std::optional<HoverIdentity> identifyAt(POINT point, DWORD /*deadlineMs*/) {
    // NOTE: COM must already be initialized on this thread (STA) by the caller.
    // We do not call CoInitializeEx here so the apartment model stays caller
    // controlled and deterministic across the worker pool.
    IUIAutomation* automation = nullptr;
    HRESULT hr = CoCreateInstance(kClsidCUIAutomation, nullptr,
                                  CLSCTX_INPROC_SERVER, kIidIUIAutomation,
                                  reinterpret_cast<void**>(&automation));
    if (FAILED(hr) || !automation) return std::nullopt;

    HoverIdentity out;
    out.hwnd = ownerWindowAt(point);

    IUIAutomationElement* element = nullptr;
    if (SUCCEEDED(automation->ElementFromPoint(point, &element)) && element) {
        // Stable identity from the runtime id when available.
        SAFEARRAY* rid = nullptr;
        if (SUCCEEDED(element->GetRuntimeId(&rid)) && rid) {
            out.elementHash = hashRuntimeId(rid);
            SafeArrayDestroy(rid);
        }

        CONTROLTYPEID ct = 0;
        if (SUCCEEDED(element->get_CurrentControlType(&ct)))
            out.controlType = controlTypeLabel(ct);

        BSTR name = nullptr;
        if (SUCCEEDED(element->get_CurrentName(&name)) && name) {
            out.name = name;
            SysFreeString(name);
        }

        element->Release();
    }

    // Fallback hash if UIA gave no runtime id (e.g. non-automation surface):
    // combine the HWND with the control type + name so "same element" still
    // deduplicates reasonably.
    if (out.elementHash == 0) {
        uint64_t h = 1469598103934665603ULL;
        auto mix = [&](const wchar_t* s) {
            for (const wchar_t* p = s; *p; ++p) {
                auto bytes = reinterpret_cast<const unsigned char*>(p);
                h ^= bytes[0];
                h *= 1099511628211ULL;
                h ^= bytes[1];
                h *= 1099511628211ULL;
            }
        };
        h ^= reinterpret_cast<uint64_t>(out.hwnd) & 0xFFFFFFFF;
        h *= 1099511628211ULL;
        mix(out.controlType.c_str());
        mix(out.name.c_str());
        out.elementHash = h;
    }

    automation->Release();
    return out;
}
