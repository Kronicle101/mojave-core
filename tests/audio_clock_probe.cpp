#include "audio_clock.hpp"
#include <tlhelp32.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1); } } while(0)
namespace { DWORD ticks{};DWORD WINAPI Tick(){return ticks;} }
int wmain(int argc,wchar_t** argv) {
    CHECK(argc==2);const DWORD pid=wcstoul(argv[1],nullptr,10);
    HANDLE modules=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,pid);CHECK(modules!=INVALID_HANDLE_VALUE);
    MODULEENTRY32W module{};module.dwSize=sizeof(module);bool found=false;
    for(BOOL next=Module32FirstW(modules,&module);next;next=Module32NextW(modules,&module)) if(!_wcsicmp(module.szModule,L"FalloutNV.exe")){found=true;break;}
    CHECK(CloseHandle(modules) && found);
    HANDLE process=OpenProcess(PROCESS_VM_READ,FALSE,pid);CHECK(process);
    auto* image=static_cast<BYTE*>(VirtualAlloc(nullptr,module.modBaseSize,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));CHECK(image);
    // Read only decoded text/data required by the profile. The game is never
    // written to, suspended or made to execute our test routines.
    const auto read=[&](size_t offset,size_t size){SIZE_T got{};CHECK(ReadProcessMemory(process,module.modBaseAddr+offset,image+offset,size,&got) && got==size);};
    read(0,0x1000);const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(image+dos->e_lfanew);
    const auto* sections=IMAGE_FIRST_SECTION(nt);
    for(WORD n=0;n<nt->FileHeader.NumberOfSections;++n) if(!memcmp(sections[n].Name,".text",5) || !memcmp(sections[n].Name,".reloc",6))
        read(sections[n].VirtualAddress,sections[n].Misc.VirtualSize);
    CHECK(CloseHandle(process));
    const DWORD adjustment=static_cast<DWORD>(reinterpret_cast<uintptr_t>(image)-reinterpret_cast<uintptr_t>(module.modBaseAddr));
    const auto& directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];size_t cursor=directory.VirtualAddress;
    CHECK(directory.Size);
    while(cursor<directory.VirtualAddress+directory.Size){
        const auto* block=reinterpret_cast<const IMAGE_BASE_RELOCATION*>(image+cursor);CHECK(block->SizeOfBlock>=8);
        const auto* entries=reinterpret_cast<const WORD*>(block+1);
        for(size_t n=0;n<(block->SizeOfBlock-8)/2;++n) if((entries[n]>>12)==IMAGE_REL_BASED_HIGHLOW)
            *reinterpret_cast<DWORD*>(image+block->VirtualAddress+(entries[n]&0xFFF))+=adjustment;
        cursor+=block->SizeOfBlock;
    }
    CHECK(fnv::audio::clock::Verify(image,module.modBaseSize));
    image[0x6DDC17]^=1;CHECK(!fnv::audio::clock::Verify(image,module.modBaseSize));image[0x6DDC17]^=1;
    *reinterpret_cast<void**>(image+0xBDF060)=reinterpret_cast<void*>(&Tick);
    auto update=reinterpret_cast<void(__thiscall*)(fnv::audio::clock::Counter*)>(image+0x6EA100);
    fnv::audio::clock::Counter counter{500,1000,0};ticks=1010;update(&counter);CHECK(counter.elapsed==510 && counter.lastTick==1010);
    ticks=11010;fnv::audio::clock::Rebase(&counter,update);CHECK(counter.elapsed==510 && counter.lastTick==11010 && counter.flags==0);
    ticks=11020;update(&counter);CHECK(counter.elapsed==520);
    counter={123,0xFFFFFFF0,1};ticks=0x10;fnv::audio::clock::Rebase(&counter,update);CHECK(counter.elapsed==123 && counter.lastTick==0x10 && counter.flags==1);
    counter.flags=0;ticks=0x20;update(&counter);CHECK(counter.elapsed==139);
    CHECK(VirtualFree(image,0,MEM_RELEASE));
    std::puts("PASS read-only FNV decoded profile; real native audio clock skips 10s background gap, retains flags and handles DWORD tick wrap");
}
