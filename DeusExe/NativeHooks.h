#pragma once

#include "Misc.h"
#include "CrashReport.h"

class CNativeHooks
{
public:
    explicit CNativeHooks(const wchar_t* const pszIniSection);
    ~CNativeHooks();
    CNativeHooks(const CNativeHooks&) = delete;
    CNativeHooks& operator=(const CNativeHooks&) = delete;

    class CFixBase //So instances can share container
    {
    public:
        virtual ~CFixBase() {}
    };

    template <class FixerClass, class UnrealClass, size_t iNativeId> //We use CRTP as the replacement function is called with another object's 'this' pointer, i.e. virtual functions plain won't work.
    class CFixBaseT : public CFixBase
    {
    public:
        ~CFixBaseT()
        {
            GNatives[iNativeId] = m_OrigFunc;
        }

    protected:
        explicit CFixBaseT(const wchar_t* const pszName)
        :m_OrigFunc(GNatives[iNativeId])
        {
            GNatives[iNativeId] = reinterpret_cast<Native>(&CFixBaseT<FixerClass, UnrealClass, iNativeId>::ReplacementFuncInternal);
            GLog->Logf(L"Installing hook '%s'.", pszName);

            //We add ourselves to the parent, that way the native function id doesn't have to be exposed.
            //Assigning into the slot, not emplace(): a rejected emplace would destroy the temporary and delete us mid-construction.
            std::unique_ptr<CFixBase>& pSlot = GetSingleton().m_ActiveHooks[iNativeId];
            assert(!pSlot); //Two fixes can't hook the same native
            pSlot.reset(this);
        }

    private:
        void ReplacementFuncInternal(FFrame& Stack, RESULT_DECL)
        {
            //At this point our 'this' pointer points to an Unreal object, not the fix object, so get its pointer.
            assert(GetSingleton().m_ActiveHooks.find(iNativeId) != GetSingleton().m_ActiveHooks.cend());
            CFixBase* const pContext = CNativeHooks::GetSingleton().m_ActiveHooks.at(iNativeId).get();
            assert(pContext);

            //Crash trace: the object this native runs on and the script function driving it.
            if(Misc::IsVerboseLogging())
            {
                CrashReport::SetScriptTrace(reinterpret_cast<UObject*>(this), Stack.Node);
            }
  
            //Called through the class, not through 'this': that would form a reference to an object of the wrong type.
            FixerClass::ReplacementFunc(reinterpret_cast<UnrealClass&>(*this), static_cast<FixerClass&>(*pContext), Stack, Result);
        }

        const Native m_OrigFunc;
    };

private:
    static CNativeHooks& GetSingleton()
    {
        assert(sm_pSingleton);
        return *sm_pSingleton;
    }

    static CNativeHooks* sm_pSingleton; //Need a singleton as classes with a wrong 'this' pointer must be able to find context.
    std::unordered_map<size_t, std::unique_ptr<CFixBase>> m_ActiveHooks;
};
