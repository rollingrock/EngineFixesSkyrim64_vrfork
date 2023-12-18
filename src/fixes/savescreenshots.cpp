#include "fixes.h"

namespace fixes
{
    const std::uint32_t regular_save = 0xF0000080;
    const std::uint32_t load_last_save = 0xD0000100;

    std::uint32_t edi_saved;

    // 0 = none, 1 = BGSSaveManager::ProcessEvents, 2 = open menu
    std::uint8_t screenshot_requested_location = 0;

    REL::Relocation<std::uintptr_t> BGSSaveLoadManager_ProcessEvents_RequestScreenshot{ BGSSaveLoadManager_ProcessEvents_RequestScreenshot_hook_offset, 0x163 };
    REL::Relocation<std::uintptr_t> MenuSave_RequestScreenshot{ MenuSave_RequestScreenshot_hook_offset, 0x56A };
    REL::Relocation<std::uintptr_t> ScreenshotJnz{ Screenshot_Jnz_hook_offset, 0x23A };  // VR 25e
    REL::Relocation<std::uintptr_t> RenderTargetHook_1{ Render_Target_Hook_1_offset, 0x365 };
    REL::Relocation<std::uintptr_t> RenderTargetHook_2{ Render_Target_Hook_2_offset, 0x3EA };
    REL::Relocation<std::uintptr_t> SaveScreenshotRequestedDword{ g_RequestSaveScreenshot_offset };
    REL::Relocation<std::uintptr_t> ScreenshotRenderOrigJnz{ Screenshot_Render_Orig_jnz_offset, 0x4D5 };

    bool PatchSaveScreenshots()
    {
        logger::trace("- save game screenshot fix -"sv);

        if (RE::GetINISetting("bUseTAA:Display")->GetBool())
        {
            logger::trace("you have TAA enabled, those fixes are uneeded"sv);
            return true;
        }

        if (*config::fixSaveScreenshots)
        {
            // lol so we have one fix that causes flicker during quicksave and one fix that causes blank journal menus
            // so just combine both, duh
            logger::trace("patching in-game save delay & blank screenshot bugs"sv);

            // with DoF enabled just use the "flicker" fix even for ingame requests
            if (RE::GetINISetting("bDoDepthOfField:Imagespace")->GetBool())
            {
                struct IsSaveRequest_Code : Xbyak::CodeGenerator
                {
                    IsSaveRequest_Code()
                    {
                        push(rax);
                        // from BGSSaveLoadManager::ProcessEvent
                        // this applies to all saves except saves done from the journal menu
                        // since menu saves do not use the event processor; the game screenshot is actually saved
                        // when you first open the menu
                        mov(rax, (std::uintptr_t)&screenshot_requested_location);
                        mov(byte[rax], 2);
                        pop(rax);
                        // we're replacing some nops here so we dont need to worry about original code...
                        jmp(ptr[rip]);
                        dq(BGSSaveLoadManager_ProcessEvents_RequestScreenshot.address() + 0xD);
                    }
                };

                IsSaveRequest_Code code;
                code.ready();

                auto& trampoline = SKSE::GetTrampoline();
                trampoline.write_branch<6>(
                    BGSSaveLoadManager_ProcessEvents_RequestScreenshot.address(),
                    trampoline.allocate(code));
            }
            // use menu fix for DoF+TAA Disabled ingame requests
            else
            {
                struct IsSaveRequest_Code : Xbyak::CodeGenerator
                {
                    IsSaveRequest_Code()
                    {
                        push(rax);
                        // from BGSSaveLoadManager::ProcessEvent
                        // this applies to all saves except saves done from the journal menu
                        // since menu saves do not use the event processor; the game screenshot is actually saved
                        // when you first open the menu
                        mov(rax, (uintptr_t)&screenshot_requested_location);
                        mov(byte[rax], 1);
                        pop(rax);
                        // we're replacing some nops here so we dont need to worry about original code...
                        jmp(ptr[rip]);
                        dq(BGSSaveLoadManager_ProcessEvents_RequestScreenshot.address() + 0xD);
                    }
                };

                IsSaveRequest_Code code;
                code.ready();

                auto& trampoline = SKSE::GetTrampoline();
                trampoline.write_branch<6>(
                    BGSSaveLoadManager_ProcessEvents_RequestScreenshot.address(),
                    trampoline.allocate(code));
            }

            // flicker fix for open menu screenshot requests
            {
                struct MenuSave_Code : Xbyak::CodeGenerator
                {
                    MenuSave_Code()
                    {
                        Xbyak::Label requestScreenshot;

                        push(rax);
                        // from open menu
                        // note: this actually ends up called twice
                        // after the first call the menu is open but has the regular game background
                        // after the second call the game background is blurred
                        mov(rax, (uintptr_t)&screenshot_requested_location);
                        mov(byte[rax], 2);
                        // since we overwrote the call to the request screenshot set function, just set it here
                        mov(rax, ptr[rip + requestScreenshot]);
                        mov(dword[rax], 1);
                        pop(rax);
                        jmp(ptr[rip]);
                        dq(MenuSave_RequestScreenshot.address() + 0x5);

                        L(requestScreenshot);
                        dq(SaveScreenshotRequestedDword.address());
                    }
                };

                MenuSave_Code code;
                code.ready();

                auto& trampoline = SKSE::GetTrampoline();
                // warning: 5 byte branch instead of 6 byte branch
                trampoline.write_branch<5>(
                    MenuSave_RequestScreenshot.address(),
                    trampoline.allocate(code));
            }

            {
                struct ScreenshotRender_Code : Xbyak::CodeGenerator
                {
                    ScreenshotRender_Code()
                    {
                        // .text:00000001412AEDAA                 test    dil, dil
                        // .text:00000001412AEDAD                 jnz     ScreenshotRenderOrigJnz
                        // .text:00000001412AEDB3                 mov     edi, 2Ah
                        // .text:00000001412AEDB8                 mov     rax, [rbp + 1F0h]
                        // .text:00000001412AEDBF                 cmp     byte ptr[rax + 18h], 0

                        push(rax);
                        mov(rax, (uintptr_t)&screenshot_requested_location);
                        cmp(byte[rax], 2);
                        je("FROM_MENU");
                        cmp(byte[rax], 1);
                        je("FROM_PROCESSEVENT");

                        L("ORIG");  // original version of code runs if no screenshot request - i think this possibly happens when you open inventory menus etc but i didnt check
                        pop(rax);
                        test(dil, dil);
                        jnz("ORIG_JNZ");
                        L("SKIP_JNZ");
                        mov(edi, 0x2A);
                        mov(rax, ptr[rbp + 0x1F0]);
                        cmp(byte[rax + 0x18], 0);
                        jmp("JMP_OUT");

                        L("FROM_MENU");  // use flicker version of fix here, all we need to do is skip jnz and rely on other patches
                        pop(rax);
                        jmp("SKIP_JNZ");

                        L("FROM_PROCESSEVENT");  // use menu version of fix here
                        mov(byte[rax], 0);       // screenshot request processed disable code for future iterations
                        pop(rax);
                        mov(edi, 0x2A);  // menu version of fix
                        cmp(byte[rbp + 0x211], 0);
                        jmp("JMP_OUT");

                        L("JMP_OUT");
                        jmp(ptr[rip]);
                        dq(ScreenshotJnz.address() + 0x19);

                        L("ORIG_JNZ");
                        jmp(ptr[rip]);
                        dq(ScreenshotRenderOrigJnz.address());
                    }
                };

                ScreenshotRender_Code code;
                code.ready();

                auto& trampoline = SKSE::GetTrampoline();
                trampoline.write_branch<6>(
                    ScreenshotJnz.address(),
                    trampoline.allocate(code));
            }

            // flicker version of fix, checks for screenshot requested from open menu
            {
                struct RenderTargetHook_1_Code : Xbyak::CodeGenerator
                {
                    RenderTargetHook_1_Code()
                    {
                        Xbyak::Label screenRequested;

                        // .text:00000001412AEED5                 mov     r9d, 4Bh
                        // .text:00000001412AEEDB                 mov     r8d, edi
                        push(rax);
                        mov(rax, (uintptr_t)&screenshot_requested_location);
                        cmp(byte[rax], 2);
                        jne("ORIG");
                        mov(rax, (uintptr_t)&edi_saved);
                        mov(dword[rax], edi);
                        mov(edi, 1);

                        L("ORIG");
                        pop(rax);
                        mov(r9d, 0x4B);
                        mov(r8d, edi);

                        jmp(ptr[rip]);
                        //.text:00000001412AEEDE                 mov     rdx, [rax + 110h]
                        // 12AEED5+0x9
                        dq(RenderTargetHook_1.address() + 0x9);
                    }
                };

                RenderTargetHook_1_Code code;
                code.ready();

                auto& trampoline = SKSE::GetTrampoline();
                trampoline.write_branch<6>(
                    RenderTargetHook_1.address(),
                    trampoline.allocate(code));
            }

            {
                struct RenderTargetHook_2_Code : Xbyak::CodeGenerator
                {
                    RenderTargetHook_2_Code()
                    {
                        // .text:00000001412AEF5A                 mov     [rbp+218h], rax
                        mov(ptr[rbp + 0x218], rax);
                        push(rax);
                        mov(rax, (uintptr_t)&screenshot_requested_location);
                        cmp(byte[rax], 2);
                        jne("ORIG");
                        mov(byte[rax], 0);
                        mov(rax, (uintptr_t)&edi_saved);
                        mov(edi, dword[rax]);

                        L("ORIG");
                        pop(rax);
                        jmp(ptr[rip]);
                        dq(RenderTargetHook_2.address() + 0x7);
                    }
                };

                RenderTargetHook_2_Code code;
                code.ready();

                auto& trampoline = SKSE::GetTrampoline();
                trampoline.write_branch<6>(
                    RenderTargetHook_2.address(),
                    trampoline.allocate(code));
            }
        }

        logger::trace("success"sv);
        return true;
    }
}
