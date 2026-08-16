#include "uia_identity.h"

#include <windows.h>

#include <objbase.h>
#include <uiautomationclient.h>

#include <cstring>
#include <functional>
#include <optional>
#include <string>

#include "identity_hash.h"

// CLSID_CUIAutomation / IID_IUIAutomation as real definitions (not DEFINE_GUID,
// which without INITGUID is only an external reference and would fail to link).
// We never link uiautomationcore.lib — the DLL is resolved by CoCreateInstance.
static const CLSID kClsidCUIAutomation = {
    0xFF48DBA4, 0x60EF, 0x4201,
    {0xAA, 0x87, 0x54, 0x10, 0x3E, 0xEF, 0x59, 0x4E}};
static const IID kIidIUIAutomation = {
    0x50A2FA23, 0xAEA1, 0x465B,
    {0x98, 0x47, 0x25, 0x06, 0x5D, 0x67, 0xFE, 0x2F}};
// IUIAutomation2 {34723aff-0c9d-49d0-9896-7ab52df8cd8a} — adds the
// connection/transaction timeout knobs used to bound a hung provider.
static const IID kIidIUIAutomation2 = {
    0x34723AFF, 0x0C9D, 0x49D0,
    {0x98, 0x96, 0x7A, 0xB5, 0x2D, 0xF8, 0xCD, 0x8A}};

namespace {

using identity_hash::hashBytes;
using identity_hash::kFnvOffsetBasis;

// FNV-1a 64-bit, used to fold a UIA runtime id (array of ints) into a compact,
// stable element hash. Runtime ids are process-unique identifiers that persist
// for the lifetime of the element, so they are the recommended stable identity.
uint64_t hashRuntimeId(const SAFEARRAY* sa) {
    uint64_t h = kFnvOffsetBasis;
    if (!sa) return h;
    auto* arr = const_cast<SAFEARRAY*>(sa);
    long lo = 0, hi = 0;
    if (FAILED(SafeArrayGetLBound(arr, 1, &lo))) return h;
    if (FAILED(SafeArrayGetUBound(arr, 1, &hi))) return h;
    for (long i = lo; i <= hi; ++i) {
        LONG v = 0;
        if (FAILED(SafeArrayGetElement(arr, &i, &v))) continue;
        hashBytes(h, &v, sizeof(v));
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

// Wall-clock budget tracker. GetTickCount64 is monotonic and cheap, and unlike
// steady_clock it is what the UIA timeout knobs are expressed in (ms).
class Deadline {
public:
    explicit Deadline(DWORD budgetMs)
        : start_(GetTickCount64()), budget_(budgetMs) {}

    bool expired() const { return remaining() == 0; }

    DWORD remaining() const {
        const ULONGLONG elapsed = GetTickCount64() - start_;
        if (elapsed >= budget_) return 0;
        return static_cast<DWORD>(budget_ - elapsed);
    }

private:
    ULONGLONG start_;
    ULONGLONG budget_;
};

// Read a BSTR property into a std::wstring, transferring ownership correctly.
// Returns false when the provider failed or returned nothing.
bool readBstr(HRESULT hr, BSTR bstr, std::wstring& out) {
    if (FAILED(hr) || !bstr) {
        if (bstr) SysFreeString(bstr);
        return false;
    }
    out.assign(bstr, SysStringLen(bstr));
    SysFreeString(bstr);
    return true;
}

}  // namespace

std::optional<HoverIdentity> identifyAt(POINT point, DWORD deadlineMs) {
    // NOTE: COM must already be initialized on this thread (STA) by the caller.
    // We do not call CoInitializeEx here so the apartment model stays caller
    // controlled and deterministic across the worker pool.
    Deadline deadline(deadlineMs ? deadlineMs : 1);

    HWND owner = ownerWindowAt(point);

    IUIAutomation* automation = nullptr;
    HRESULT hr = CoCreateInstance(kClsidCUIAutomation, nullptr,
                                  CLSCTX_INPROC_SERVER, kIidIUIAutomation,
                                  reinterpret_cast<void**>(&automation));
    if (FAILED(hr) || !automation) {
        // No UIA at all. A window alone is still a usable (weak) identity; with
        // nothing at all there is nothing to report and the caller counts a
        // resolution failure.
        if (!owner) return std::nullopt;
        HoverIdentity weak;
        weak.hwnd = owner;
        uint64_t h = kFnvOffsetBasis;
        const uint64_t hw = reinterpret_cast<uintptr_t>(owner);
        hashBytes(h, &hw, sizeof(hw));
        weak.elementHash = h;
        return weak;
    }

    // Bound cross-process provider calls where the platform supports it. Without
    // this, ElementFromPoint against a hung window blocks for the UIA default
    // (two minutes) regardless of any budget we track on our side.
    IUIAutomation2* automation2 = nullptr;
    if (SUCCEEDED(automation->QueryInterface(
            kIidIUIAutomation2, reinterpret_cast<void**>(&automation2))) &&
        automation2) {
        const DWORD budget = deadline.remaining();
        automation2->put_ConnectionTimeout(budget);
        automation2->put_TransactionTimeout(budget);
        // Never let a probe move focus in the target app.
        automation2->put_AutoSetFocus(FALSE);
    }

    HoverIdentity out;
    out.hwnd = owner;

    // Extra discriminators used only by the fallback hash. Collected here so the
    // fallback can distinguish sibling elements that share control type + name.
    std::wstring automationId;
    std::wstring className;
    RECT bounds{};
    bool haveBounds = false;
    int siblingIndex = -1;
    uint64_t parentRuntimeHash = 0;
    bool haveRuntimeId = false;

    IUIAutomationElement* element = nullptr;
    if (!deadline.expired() &&
        SUCCEEDED(automation->ElementFromPoint(point, &element)) && element) {
        // Stable identity from the runtime id when available.
        if (!deadline.expired()) {
            SAFEARRAY* rid = nullptr;
            if (SUCCEEDED(element->GetRuntimeId(&rid)) && rid) {
                out.elementHash = hashRuntimeId(rid);
                haveRuntimeId = true;
                SafeArrayDestroy(rid);
            }
        }

        if (!deadline.expired()) {
            CONTROLTYPEID ct = 0;
            if (SUCCEEDED(element->get_CurrentControlType(&ct)))
                out.controlType = controlTypeLabel(ct);
        }

        if (!deadline.expired()) {
            BSTR name = nullptr;
            readBstr(element->get_CurrentName(&name), name, out.name);
        }

        // The remaining properties only feed the fallback hash, so skip them
        // entirely once we already have a runtime id or the budget is gone.
        if (!haveRuntimeId) {
            if (!deadline.expired()) {
                BSTR aid = nullptr;
                readBstr(element->get_CurrentAutomationId(&aid), aid,
                         automationId);
            }
            if (!deadline.expired()) {
                BSTR cls = nullptr;
                readBstr(element->get_CurrentClassName(&cls), cls, className);
            }
            if (!deadline.expired()) {
                if (SUCCEEDED(element->get_CurrentBoundingRectangle(&bounds)))
                    haveBounds = true;
            }
            // Position among same-type siblings + the parent's runtime id: this
            // is what actually separates two list items or two identically
            // labelled buttons inside the same container.
            if (!deadline.expired()) {
                IUIAutomationTreeWalker* walker = nullptr;
                if (SUCCEEDED(automation->get_RawViewWalker(&walker)) &&
                    walker) {
                    IUIAutomationElement* parent = nullptr;
                    if (SUCCEEDED(walker->GetParentElement(element, &parent)) &&
                        parent) {
                        SAFEARRAY* prid = nullptr;
                        if (SUCCEEDED(parent->GetRuntimeId(&prid)) && prid) {
                            parentRuntimeHash = hashRuntimeId(prid);
                            SafeArrayDestroy(prid);
                        }
                        parent->Release();
                    }
                    // Count preceding siblings, bounded by the deadline so a
                    // pathological tree cannot stall the probe.
                    int index = 0;
                    IUIAutomationElement* cursor = nullptr;
                    if (SUCCEEDED(walker->GetPreviousSiblingElement(
                            element, &cursor))) {
                        while (cursor && !deadline.expired() && index < 4096) {
                            ++index;
                            IUIAutomationElement* prev = nullptr;
                            if (FAILED(walker->GetPreviousSiblingElement(
                                    cursor, &prev))) {
                                cursor->Release();
                                cursor = nullptr;
                                break;
                            }
                            cursor->Release();
                            cursor = prev;
                        }
                        if (cursor) cursor->Release();
                        siblingIndex = index;
                    }
                    walker->Release();
                }
            }
        }

        element->Release();
    }

    // Fallback hash if UIA gave no runtime id (e.g. non-automation surface).
    // Composed from every discriminator we managed to read, each length-framed
    // and tagged so distinct field layouts cannot fold to the same value. The
    // full HWND pointer, the element's bounding rectangle, its AutomationId /
    // ClassName, and its index under its parent are what stop sibling elements
    // with matching control type + name from colliding.
    if (!haveRuntimeId) {
        identity_hash::FallbackInputs fi;
        fi.windowHandle = reinterpret_cast<uintptr_t>(out.hwnd);
        fi.controlType = out.controlType;
        fi.name = out.name;
        fi.automationId = automationId;
        fi.className = className;
        fi.boundsBytes = &bounds;
        fi.boundsLen = sizeof(bounds);
        fi.haveBounds = haveBounds;
        fi.siblingIndex = siblingIndex;
        fi.parentRuntimeHash = parentRuntimeHash;
        out.elementHash = identity_hash::composeFallback(fi);
    }

    if (automation2) automation2->Release();
    automation->Release();

    // Nothing identifiable at all (no window, no UIA element): report failure so
    // the caller can count it rather than render an empty card.
    if (!out.hwnd && !haveRuntimeId && out.controlType.empty() &&
        out.name.empty()) {
        return std::nullopt;
    }
    return out;
}
