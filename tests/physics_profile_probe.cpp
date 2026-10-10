#include "physics.hpp"
#include "simulation_clock.hpp"
#include <tlhelp32.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1); } } while(0)
void WINAPI UnexpectedSleep(DWORD) { CHECK(false); }
int wmain(int argc,wchar_t** argv) {
    CHECK(argc==2);const DWORD pid=wcstoul(argv[1],nullptr,10);
    HANDLE modules=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,pid);CHECK(modules!=INVALID_HANDLE_VALUE);
    MODULEENTRY32W module{};module.dwSize=sizeof(module);bool found=false;
    for(BOOL next=Module32FirstW(modules,&module);next;next=Module32NextW(modules,&module)) if(!_wcsicmp(module.szModule,L"FalloutNV.exe")){found=true;break;}
    CHECK(CloseHandle(modules) && found);
    HANDLE process=OpenProcess(PROCESS_VM_READ,FALSE,pid);CHECK(process);
    auto* image=static_cast<BYTE*>(VirtualAlloc(nullptr,module.modBaseSize,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));CHECK(image);
    const auto read=[&](size_t offset,size_t size){SIZE_T got{};CHECK(ReadProcessMemory(process,module.modBaseAddr+offset,image+offset,size,&got) && got==size);};
    read(0,0x1000);const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(image+dos->e_lfanew);const auto* sections=IMAGE_FIRST_SECTION(nt);
    for(WORD n=0;n<nt->FileHeader.NumberOfSections;++n) if(!memcmp(sections[n].Name,".text",5) || !memcmp(sections[n].Name,".reloc",6) || !memcmp(sections[n].Name,".rdata",6))
        read(sections[n].VirtualAddress,sections[n].Misc.VirtualSize);
    CHECK(CloseHandle(process));
    const DWORD adjustment=static_cast<DWORD>(reinterpret_cast<uintptr_t>(image)-reinterpret_cast<uintptr_t>(module.modBaseAddr));
    const auto& directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];size_t cursor=directory.VirtualAddress;CHECK(directory.Size);
    while(cursor<directory.VirtualAddress+directory.Size){
        const auto* block=reinterpret_cast<const IMAGE_BASE_RELOCATION*>(image+cursor);CHECK(block->SizeOfBlock>=8);
        const auto* entries=reinterpret_cast<const WORD*>(block+1);
        for(size_t n=0;n<(block->SizeOfBlock-8)/2;++n) if((entries[n]>>12)==IMAGE_REL_BASED_HIGHLOW)
            *reinterpret_cast<DWORD*>(image+block->VirtualAddress+(entries[n]&0xFFF))+=adjustment;
        cursor+=block->SizeOfBlock;
    }
    const BYTE prologue[]={0x55,0x8B,0xEC,0x83,0xEC,0x34,0x89,0x4D,0xEC};
    CHECK(!memcmp(image+0x6A4EE0,prologue,sizeof(prologue)));
    CHECK(fnv::physics::VerifyScheduler(image,module.modBaseSize));
    image[0x46F2D8]^=1;CHECK(!fnv::physics::VerifyScheduler(image,module.modBaseSize));image[0x46F2D8]^=1;
    auto native=reinterpret_cast<fnv::physics::NativeTimerUpdate>(image+0x6A4EE0);
    *reinterpret_cast<void**>(image+0xBDF104)=reinterpret_cast<void*>(&UnexpectedSleep);
    // Execute the stock Havok timestep load/multiply/store instructions, copied
    // into a private stub. No game world, asset, process or save is modified.
    BYTE* stub=image+0x200;
    CHECK(image[0x86AF33]==0xD9 && image[0x86AF3A]==0xD8 && image[0x86AF42]==0xD9);
    memcpy(stub,image+0x86AF33,6);memcpy(stub+6,image+0x86AF3A,6);memcpy(stub+12,image+0x86AF42,6);stub[18]=0xC3;
    for(float multiplier:{1.f,.3f,2.f}) for(int fps:{30,60,120,144,165,240,500,1000,10000}) {
        *reinterpret_cast<float*>(image+0xDAC3A0)=multiplier;
        fnv::physics::Timer timer{};timer.sameFrame=true;
        const float havok=fnv::physics::AdvanceTimer(&timer,0,1.0/fps,.016f,false,false,native);
        CHECK(!timer.sameFrame && timer.realDeltaSeconds>0);
        if(fps>=120) CHECK(!memcmp(&havok,&timer.realDeltaSeconds,4));
        *reinterpret_cast<float*>(image+0xE67B38)=havok;
        reinterpret_cast<void(__cdecl*)()>(stub)();const float solverStep=*reinterpret_cast<float*>(image+0xE677AC);
        if(fps>=120) CHECK(std::abs(solverStep-timer.deltaSeconds)<=timer.deltaSeconds*1e-6f);
        CHECK(solverStep>0 && solverStep<=timer.deltaSeconds+1e-6f);
        fnv::physics::AdvanceTimer(&timer,0,0,.016f,false,false,native);CHECK(timer.sameFrame && timer.deltaSeconds==0 && timer.realDeltaSeconds==0);
        fnv::physics::AdvanceTimer(&timer,0,1.0/fps,.016f,false,false,native);CHECK(!timer.sameFrame && timer.deltaSeconds>0);
    }
    *reinterpret_cast<float*>(image+0xDAC3A0)=1;
    fnv::physics::Timer sequence{};
    for (double elapsed:{.001,.0003,.008,.003,.025,.05,.0001}) {
        const float havok=fnv::physics::AdvanceTimer(&sequence,0,elapsed,.016f,false,false,native);
        CHECK(!sequence.sameFrame && sequence.realDeltaSeconds>0 && havok<=sequence.realDeltaSeconds);
    }
    sequence.disabled=1;
    CHECK(fnv::physics::AdvanceTimer(&sequence,0,.005,.016f,false,true,native)==.016f && sequence.deltaSeconds==0);
    sequence.disabled=0;
    fnv::physics::AdvanceTimer(&sequence,0,.005,.016f,false,false,native);CHECK(!sequence.sameFrame && sequence.deltaSeconds>0);
    auto scheduler=reinterpret_cast<void(__cdecl*)(float,bool,bool)>(image+0x866760);
    const fnv::physics::SimulationStage stage{reinterpret_cast<float*>(image+0xDAFE64),reinterpret_cast<float*>(image+0xDAFE68),
        reinterpret_cast<float*>(image+0xE677AC),reinterpret_cast<float*>(image+0xE677B0),reinterpret_cast<uint32_t*>(image+0xE677B4)};
    for(float scale:{.3f,1.f,2.f})for(int fps:{30,120,144,165,1000,10000}){
        fnv::physics::SimulationClock clock;fnv::physics::Timer render{};
        *reinterpret_cast<float*>(image+0xDAC3A0)=scale;*reinterpret_cast<float*>(image+0xE67B38)=.016f;
        *reinterpret_cast<uint32_t*>(image+0xE677B8)=0;*stage.remainder=0;
        const float quantum=.016f*scale;uint64_t ticks{};double consumed{};
        for(int frame=0;frame<fps;++frame){
            fnv::physics::AdvanceTimer(&render,0,1.0/fps,.016f,false,false,native);
            const auto batch=clock.Consume(render.deltaSeconds,quantum);scheduler(batch.seconds,false,false);
            fnv::physics::PublishSimulationStep(stage,batch,quantum);
            CHECK(*stage.input==batch.seconds && *stage.count==batch.count && !render.sameFrame && render.deltaSeconds>0);
            if(!batch.count)CHECK(*stage.delta==0 && *stage.step==0);
            else {reinterpret_cast<void(__cdecl*)()>(stub)();CHECK(*stage.step==quantum);}
            ticks+=batch.count;consumed+=render.deltaSeconds;
        }
        CHECK(std::abs(double(ticks)*quantum+clock.Remainder()-consumed)<1e-8);
    }
    CHECK(VirtualFree(image,0,MEM_RELEASE));
    std::puts("PASS actual FNV BSTimer and scheduler ABI + Havok step setup: fixed quantum, idle-frame controller signals, 30..10000 FPS and scales 0.3/1/2");
}
