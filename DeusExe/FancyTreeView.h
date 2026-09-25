#pragma once

class CFancyTreeView
{
public:
    enum class EItemState { UNCHECKED, CHECKED, HALF };

    CFancyTreeView() = default;
    ~CFancyTreeView();

    void Init(const HWND hwndTreeView, const HWND hWndButtonUp, const HWND hWndButtonDown);
    HWND GetHWnd() const { return m_hWnd; }

    HTREEITEM GetRoot() const { return TVI_ROOT; }
    unsigned int GetCount() const { return TreeView_GetCount(m_hWnd); }
    HTREEITEM InsertItem(const wchar_t* const pszText, const HTREEITEM hParent);
    HTREEITEM InsertItemUnique(const wchar_t* const pszText, const HTREEITEM hParent);

    void SelectFirstItem();
    void SetItemState(const HTREEITEM hItem, const EItemState State) { TreeView_SetItemState(m_hWnd, hItem, ItemStateToStateImageMask(State), TVIS_STATEIMAGEMASK); } //!< Sets only the visual checkbox image; use ApplyCheckState() to also update the item's parents and children
    EItemState GetItemState(const HTREEITEM hItem) const { return StateImageMaskToItemState(TreeView_GetItemState(m_hWnd, hItem, TVIS_STATEIMAGEMASK)); }

    /**
    Sets an item's checkbox state and propagates it: the whole subtree beneath the item is set to the same state, and every ancestor's state is recomputed (checked/half/unchecked).
    Done explicitly rather than through TVN_ITEMCHANGED notifications; see TreeSubclassProc.
    */
    void ApplyCheckState(const HTREEITEM hItem, const EItemState State);

    void GetItemText(const HTREEITEM hItem, wchar_t* const pszBuf, const size_t iBufCount) const;

    /**
    Finds an item's leaf, which can be the item itself
    */
    HTREEITEM FindLeaf(const HTREEITEM hItem) const;
    HTREEITEM FindNextLeaf(const HTREEITEM hItem) const;

    void SwapItems(const HTREEITEM hItem1, const HTREEITEM hItem2);
    void MoveItemUp();
    void MoveItemDown();

    BOOL HandleNotify(const NMHDR* const pNMH);

private:
    class CTreeItem
    {
        friend class CFancyTreeView;
    public:
        explicit CTreeItem(const unsigned int iSortKey)
        :m_iSortKey(iSortKey)
        {
        }

    private:
        unsigned int m_iSortKey; //!< Used for move up/down
        unsigned int m_iChildrenChecked = 0; //!< Number of direct children currently checked
        unsigned int m_iChildrenHalfChecked = 0; //!< Number of direct children currently half-checked
        unsigned int m_iNumChildren = 0; //!< Total number of direct children
    };

    
    static EItemState StateImageMaskToItemState(const UINT iState) //Unfortunately there's no reverse INDEXTOSTATEIMAGEMASK macro
    {
        const UINT iIndex = iState >> 12;
        return iIndex >= 1 && iIndex <= 3 ? static_cast<EItemState>(iIndex - 1) : EItemState::UNCHECKED; //0 means the item has no state image at all
    }
    static UINT ItemStateToStateImageMask(const EItemState State) { return INDEXTOSTATEIMAGEMASK(static_cast<unsigned int>(State)+1); }
    
    CTreeItem* TreeItemFromHandle(const HTREEITEM hItem); //!< Null if the item is gone or its lParam couldn't be read

    /**
    Toggles an item's checkbox in response to user input (mouse or keyboard), forcing a simple two-state (checked/unchecked) result even for half-checked parents, then propagates via ApplyCheckState().
    */
    void ToggleUserCheck(const HTREEITEM hItem);

    void ApplyStateToSubtree(const HTREEITEM hItem, const EItemState State); //!< Recursively sets hItem and all of its descendants to State, updating their child counters
    void UpdateAncestors(const HTREEITEM hItem, const EItemState ChildOldState, const EItemState ChildNewState); //!< Walks up from hItem recomputing each ancestor's counters and checkbox state

    HIMAGELIST CreateCheckboxStateImageList() const;

    /**
    Subclass procedure for the tree-view. We drive checkbox toggling ourselves here so it behaves identically on Windows and WINE, instead of depending on TVN_ITEMCHANGED notifications (which WINE doesn't send for checkbox changes).
    */
    static LRESULT CALLBACK TreeSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

    HWND m_hWnd = NULL;
    HWND m_hWndButtonUp = NULL;
    HWND m_hWndButtonDown = NULL;
    HIMAGELIST m_hStateImageList = NULL; //!< Our own checkbox state-image list, owned by us and freed in the destructor
    
    std::deque<CTreeItem> m_Items; //!< Deque so we can push_back items without invalidating pointers
};