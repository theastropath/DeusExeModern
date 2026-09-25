#include "stdafx.h"
#include "FancyTreeView.h"

CFancyTreeView::~CFancyTreeView()
{
    if(m_hStateImageList)
    {
        ImageList_Destroy(m_hStateImageList);
    }
}

void CFancyTreeView::Init(const HWND hwndTreeView, const HWND hWndButtonUp, const HWND hWndButtonDown)
{
    assert(hwndTreeView);
    assert(hWndButtonUp);
    assert(hWndButtonDown);

    m_hWnd = hwndTreeView;
    m_hWndButtonUp = hWndButtonUp;
    m_hWndButtonDown = hWndButtonDown;
    
    SetWindowTheme(m_hWnd, L"explorer", nullptr); //Exporer-style arrows instead of pluses to expand list items

    //Enable checkboxes via the TVS_CHECKBOXES style: WINE only builds the checkbox state-image list when it's set (it ignores the extended checkbox styles).
    SetWindowLongPtr(m_hWnd, GWL_STYLE, GetWindowLongPtr(m_hWnd, GWL_STYLE) | TVS_CHECKBOXES);

    //Supply our own state-image list with an indeterminate image (index 3); WINE's has only unchecked/checked, so the half-checked parent state wouldn't render.
    m_hStateImageList = CreateCheckboxStateImageList();
    if(m_hStateImageList)
    {
        const HIMAGELIST hOldStateImageList = TreeView_SetImageList(m_hWnd, m_hStateImageList, TVSIL_STATE);
        if(hOldStateImageList)
        {
            ImageList_Destroy(hOldStateImageList); //Free the control's auto-generated checkbox list we just replaced
        }
    }

    TreeView_SetExtendedStyle(m_hWnd, TVS_EX_PARTIALCHECKBOXES | TVS_EX_AUTOHSCROLL, TVS_EX_PARTIALCHECKBOXES | TVS_EX_AUTOHSCROLL);

    //Handle checkbox toggling ourselves (see TreeSubclassProc): WINE doesn't emit TVN_ITEMCHANGED for checkbox changes, so notification-driven child ticking fails there.
    SetWindowSubclass(m_hWnd, &CFancyTreeView::TreeSubclassProc, 0, reinterpret_cast<DWORD_PTR>(this));
}

HIMAGELIST CFancyTreeView::CreateCheckboxStateImageList() const
{
    //State image index 0 means "no image", so the checkboxes live at 1 (unchecked), 2 (checked) and 3 (indeterminate), matching ItemStateToStateImageMask().
    const int iImageCount = 4;

    const HTHEME hTheme = OpenThemeData(m_hWnd, L"BUTTON");
    const HDC hdcScreen = GetDC(nullptr);

    //Draw at the size the theme wants, falling back to the system checkmark metrics when unthemed
    SIZE Size = { 0, 0 };
    if(hTheme)
    {
        GetThemePartSize(hTheme, hdcScreen, BP_CHECKBOX, CBS_UNCHECKEDNORMAL, nullptr, TS_DRAW, &Size);
    }
    if(Size.cx <= 0 || Size.cy <= 0)
    {
        Size.cx = GetSystemMetrics(SM_CXMENUCHECK);
        Size.cy = GetSystemMetrics(SM_CYMENUCHECK);
    }
    const int iWidth = static_cast<int>(Size.cx);
    const int iHeight = static_cast<int>(Size.cy);

    const HDC hdc = CreateCompatibleDC(hdcScreen);
    const HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, iWidth * iImageCount, iHeight);

    HIMAGELIST hImageList = NULL;
    if(hdc && hBitmap) //Without a drawing surface, let the control keep its own checkbox images
    {
        const HBITMAP hBitmapOld = static_cast<HBITMAP>(SelectObject(hdc, hBitmap));

        RECT rcFill = { 0, 0, iWidth * iImageCount, iHeight };
        FillRect(hdc, &rcFill, GetSysColorBrush(COLOR_WINDOW));

        for(int i = 1; i < iImageCount; i++)
        {
            const RECT rc = { iWidth * i, 0, iWidth * (i + 1), iHeight };
            if(hTheme)
            {
                DrawThemeBackground(hTheme, hdc, BP_CHECKBOX, i == 2 ? CBS_CHECKEDNORMAL : i == 3 ? CBS_MIXEDNORMAL : CBS_UNCHECKEDNORMAL, &rc, nullptr);
            }
            else
            {
                const UINT uFrameState = DFCS_BUTTONCHECK | (i == 2 ? DFCS_CHECKED : i == 3 ? DFCS_CHECKED | DFCS_INACTIVE : 0u);
                DrawFrameControl(hdc, const_cast<RECT*>(&rc), DFC_BUTTON, uFrameState);
            }
        }

        SelectObject(hdc, hBitmapOld);

        //Themed checkboxes are alpha-blended; keep them 32-bit and let them composite over the tree's background
        hImageList = ImageList_Create(iWidth, iHeight, hTheme ? ILC_COLOR32 : ILC_COLOR | ILC_MASK, iImageCount, 0);
        if(hImageList)
        {
            if(hTheme)
            {
                ImageList_Add(hImageList, hBitmap, nullptr);
            }
            else
            {
                ImageList_AddMasked(hImageList, hBitmap, GetSysColor(COLOR_WINDOW));
            }
        }
    }

    if(hBitmap)
    {
        DeleteObject(hBitmap);
    }
    if(hdc)
    {
        DeleteDC(hdc);
    }
    ReleaseDC(nullptr, hdcScreen);
    if(hTheme)
    {
        CloseThemeData(hTheme);
    }

    return hImageList;
}

HTREEITEM CFancyTreeView::InsertItem(const wchar_t* const pszText, const HTREEITEM hParent)
{
    m_Items.emplace_back(GetCount());

    TVINSERTSTRUCT Item = {};
    Item.hParent = hParent;
    Item.hInsertAfter = TVI_LAST;
    Item.itemex.mask = TVIF_TEXT | TVIF_PARAM;
    Item.itemex.pszText = const_cast<wchar_t*>(pszText);
    Item.itemex.lParam = reinterpret_cast<LPARAM>(&m_Items.back());

    const HTREEITEM hItem = TreeView_InsertItem(m_hWnd, &Item);

    if(hParent!=TVI_ROOT)
    {
        CTreeItem* const pParentItem = TreeItemFromHandle(hParent);
        if(pParentItem)
        {
            pParentItem->m_iNumChildren++;
        }

        //An unchecked item was added, so set parent to half checked if necessary
        const EItemState ParentOld = GetItemState(hParent);
        if(ParentOld == EItemState::CHECKED)
        {
            SetItemState(hParent, EItemState::HALF);
            UpdateAncestors(hParent, ParentOld, EItemState::HALF); //Else the grandparents keep counting this one as fully checked
        }
    }

    return hItem;
}

HTREEITEM CFancyTreeView::InsertItemUnique(const wchar_t* const pszText, const HTREEITEM hParent)
{
    //Unfortunately there doesn't seem to be a better way to do this without keeping our own tiered string-to-HTREEITEM-map.
    for(HTREEITEM hChild = TreeView_GetChild(m_hWnd, hParent); hChild; hChild = TreeView_GetNextSibling(m_hWnd, hChild))
    {
        wchar_t szBuf[MAX_PATH];
        GetItemText(hChild, szBuf, _countof(szBuf));
        if(_wcsicmp(szBuf, pszText) == 0)
        {
            return hChild;
        }
    }
    return InsertItem(pszText, hParent);
}

void CFancyTreeView::SelectFirstItem()
{
    const HTREEITEM hRootItem = TreeView_GetRoot(m_hWnd);
    if(hRootItem)
    {
        TreeView_SelectItem(m_hWnd, hRootItem);
    }
}

void CFancyTreeView::GetItemText(const HTREEITEM hItem, wchar_t* const pszBuf, const size_t iBufCount) const
{
    assert(hItem);
    assert(iBufCount > 0);

    pszBuf[0] = '\0'; //A failed TreeView_GetItem leaves the buffer untouched

    TVITEMEX Item = {};
    Item.hItem = hItem;
    Item.mask = TVIF_TEXT;
    Item.cchTextMax = static_cast<int>(iBufCount);
    Item.pszText = pszBuf;
    TreeView_GetItem(m_hWnd, &Item);
}

HTREEITEM CFancyTreeView::FindLeaf(const HTREEITEM hItem) const
{
    assert(hItem);
    HTREEITEM hLeaf = hItem;
    for(HTREEITEM hChild = TreeView_GetChild(m_hWnd, hItem); hChild; hChild = TreeView_GetChild(m_hWnd, hChild))
    {
        hLeaf = hChild;
    }
    return hLeaf;
}

HTREEITEM CFancyTreeView::FindNextLeaf(const HTREEITEM hItem) const
{
    assert(hItem);
    
    //Try children
    const HTREEITEM hLeaf = FindLeaf(hItem);
    if(hLeaf == TVI_ROOT) //No child found for root (i.e. empty list)
    {
        return NULL;
    }
    if(hLeaf!=hItem)
    {
        return hLeaf;
    }

    //Try children of siblings
    const HTREEITEM hSibling = TreeView_GetNextSibling(m_hWnd, hItem);
    if(hSibling)
    {
        return FindLeaf(hSibling);
    }
    
    //Try siblings of parent
    for(HTREEITEM hParent = TreeView_GetParent(m_hWnd, hItem); hParent; hParent = TreeView_GetParent(m_hWnd, hParent))
    {
        const HTREEITEM hParentSibling = TreeView_GetNextSibling(m_hWnd, hParent);
        if(hParentSibling)
        {
            return FindLeaf(hParentSibling);
        }
    }

    return NULL;
}

void CFancyTreeView::SwapItems(const HTREEITEM hItem1, const HTREEITEM hItem2)
{
    assert(hItem1);
    assert(hItem2);
    //Swap the items' sort keys, then re-sort by those
    CTreeItem* const pItem1 = TreeItemFromHandle(hItem1);
    CTreeItem* const pItem2 = TreeItemFromHandle(hItem2);
    if(!pItem1 || !pItem2)
    {
        return;
    }
    std::swap(pItem1->m_iSortKey, pItem2->m_iSortKey);
    
    TVSORTCB SortCB = {}; //Zeroed: lParam is passed on to the compare function, so it can't be left indeterminate
    SortCB.hParent = TVI_ROOT;

    SortCB.lpfnCompare = [](const LPARAM lParam1, const LPARAM lParam2, const LPARAM /*lParamSort*/)
    {
        const CTreeItem* const pItem1 = reinterpret_cast<CTreeItem*>(lParam1);
        assert(pItem1);
        const CTreeItem* const pItem2 = reinterpret_cast<CTreeItem*>(lParam2);
        assert(pItem2);
        return pItem1->m_iSortKey < pItem2->m_iSortKey ? -1 : pItem1->m_iSortKey == pItem2->m_iSortKey ? 0 : 1;
    };

    TreeView_SortChildrenCB(m_hWnd, &SortCB, 0);
}

void CFancyTreeView::MoveItemUp()
{
    const HTREEITEM hItem = TreeView_GetSelection(m_hWnd);
    const HTREEITEM hPrevSibling = hItem ? TreeView_GetPrevSibling(m_hWnd, hItem) : NULL;
    if(!hPrevSibling)
    {
        return;
    }
    SwapItems(hItem, hPrevSibling);

    EnableWindow(m_hWndButtonUp, TreeView_GetPrevSibling(m_hWnd, hItem) != NULL);
    EnableWindow(m_hWndButtonDown, TRUE);
}

void CFancyTreeView::MoveItemDown()
{ 
    const HTREEITEM hItem = TreeView_GetSelection(m_hWnd);
    const HTREEITEM hNextSibling = hItem ? TreeView_GetNextSibling(m_hWnd, hItem) : NULL;
    if(!hNextSibling)
    {
        return;
    }
    SwapItems(hItem, hNextSibling);

    EnableWindow(m_hWndButtonUp, TRUE);
    EnableWindow(m_hWndButtonDown, TreeView_GetNextSibling(m_hWnd, hItem) != NULL);
}

void CFancyTreeView::ToggleUserCheck(const HTREEITEM hItem)
{
    assert(hItem);
    //A user click/keypress is always a simple two-state toggle; a half-checked parent becomes fully checked (which then checks all of its children)
    const EItemState NewState = GetItemState(hItem) == EItemState::CHECKED ? EItemState::UNCHECKED : EItemState::CHECKED;
    ApplyCheckState(hItem, NewState);
}

void CFancyTreeView::ApplyCheckState(const HTREEITEM hItem, const EItemState State)
{
    assert(hItem);
    const EItemState OldState = GetItemState(hItem);
    ApplyStateToSubtree(hItem, State);
    UpdateAncestors(hItem, OldState, State);
}

void CFancyTreeView::ApplyStateToSubtree(const HTREEITEM hItem, const EItemState State)
{
    assert(hItem);
    SetItemState(hItem, State);

    unsigned int iNumChildren = 0;
    for(HTREEITEM hChild = TreeView_GetChild(m_hWnd, hItem); hChild; hChild = TreeView_GetNextSibling(m_hWnd, hChild))
    {
        ApplyStateToSubtree(hChild, State);
        iNumChildren++;
    }

    //Every child now shares the item's state, so its counters follow directly from it
    CTreeItem* const pItem = TreeItemFromHandle(hItem);
    if(pItem)
    {
        pItem->m_iChildrenChecked = State == EItemState::CHECKED ? iNumChildren : 0;
        pItem->m_iChildrenHalfChecked = 0;
    }
}

void CFancyTreeView::UpdateAncestors(const HTREEITEM hItem, const EItemState ChildOldState, const EItemState ChildNewState)
{
    assert(hItem);

    EItemState ChildOld = ChildOldState;
    EItemState ChildNew = ChildNewState;
    for(HTREEITEM hParent = TreeView_GetParent(m_hWnd, hItem); hParent; hParent = TreeView_GetParent(m_hWnd, hParent))
    {
        CTreeItem* const pParentItem = TreeItemFromHandle(hParent);
        if(!pParentItem)
        {
            break;
        }
        CTreeItem& ParentItem = *pParentItem;

        if(ChildOld == EItemState::CHECKED) ParentItem.m_iChildrenChecked--;
        else if(ChildOld == EItemState::HALF) ParentItem.m_iChildrenHalfChecked--;
        if(ChildNew == EItemState::CHECKED) ParentItem.m_iChildrenChecked++;
        else if(ChildNew == EItemState::HALF) ParentItem.m_iChildrenHalfChecked++;

        assert(ParentItem.m_iChildrenChecked + ParentItem.m_iChildrenHalfChecked <= ParentItem.m_iNumChildren);

        EItemState ParentNew;
        if(ParentItem.m_iNumChildren > 0 && ParentItem.m_iChildrenChecked == ParentItem.m_iNumChildren)
            ParentNew = EItemState::CHECKED;
        else if(ParentItem.m_iChildrenChecked == 0 && ParentItem.m_iChildrenHalfChecked == 0)
            ParentNew = EItemState::UNCHECKED;
        else
            ParentNew = EItemState::HALF;

        const EItemState ParentOld = GetItemState(hParent);
        if(ParentNew == ParentOld)
        {
            break; //The parent's own state didn't change, so nothing above it changes either
        }

        SetItemState(hParent, ParentNew);
        ChildOld = ParentOld;
        ChildNew = ParentNew;
    }
}

BOOL CFancyTreeView::HandleNotify(const NMHDR* const pNMH)
{
    assert(pNMH);
    switch(pNMH->code)
    {
    case TVN_SELCHANGING:
    {
        const NMTREEVIEW * const pItemInfo = reinterpret_cast<const NMTREEVIEW*>(pNMH);

        if(m_hWndButtonUp && m_hWndButtonDown)
        {
            const bool bIsTopLevel = TreeView_GetParent(m_hWnd, pItemInfo->itemNew.hItem) == NULL;
            const bool bIsTopItem = TreeView_GetPrevSibling(m_hWnd, pItemInfo->itemNew.hItem) == NULL;
            const bool bIsBottomItem = TreeView_GetNextSibling(m_hWnd, pItemInfo->itemNew.hItem) == NULL;

            EnableWindow(m_hWndButtonUp, bIsTopLevel && !bIsTopItem);
            EnableWindow(m_hWndButtonDown, bIsTopLevel && !bIsBottomItem);
        }
    }
    return FALSE;

    }
    return FALSE;
}

LRESULT CALLBACK CFancyTreeView::TreeSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    CFancyTreeView* const pThis = reinterpret_cast<CFancyTreeView*>(dwRefData);
    assert(pThis);

    switch(uMsg)
    {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    {
        //Toggle the checkbox ourselves when its state icon is clicked, so ticking/unticking a parent applies to all of its children
        TVHITTESTINFO HitTest;
        HitTest.pt.x = static_cast<short>(LOWORD(lParam));
        HitTest.pt.y = static_cast<short>(HIWORD(lParam));
        if(TreeView_HitTest(hWnd, &HitTest) && (HitTest.flags & TVHT_ONITEMSTATEICON))
        {
            pThis->ToggleUserCheck(HitTest.hItem);
            TreeView_SelectItem(hWnd, HitTest.hItem); //Match the previous behaviour of selecting the row whose box was clicked
            SetFocus(hWnd);
            return 0; //Swallow the click so the control doesn't also run its own (platform-dependent) checkbox toggle
        }
        break;
    }

    case WM_KEYDOWN:
        if(wParam == VK_SPACE)
        {
            const HTREEITEM hSelected = TreeView_GetSelection(hWnd);
            if(hSelected)
            {
                pThis->ToggleUserCheck(hSelected);
                return 0; //Swallow it so the control doesn't cycle through the half-checked image instead
            }
        }
        break;

    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, &CFancyTreeView::TreeSubclassProc, uIdSubclass);
        break;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

CFancyTreeView::CTreeItem* CFancyTreeView::TreeItemFromHandle(const HTREEITEM hItem)
{
    assert(hItem);
    TVITEMEX Item = {};
    Item.hItem = hItem;
    Item.mask = TVIF_HANDLE | TVIF_PARAM;
    if(!TreeView_GetItem(m_hWnd, &Item)) //On failure lParam is left uninitialized, so it can't be dereferenced
    {
        return nullptr;
    }

    return reinterpret_cast<CTreeItem*>(Item.lParam);
}
