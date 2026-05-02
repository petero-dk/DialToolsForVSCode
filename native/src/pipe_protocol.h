#pragma once
#include <cstdint>

namespace DialTools {

enum class Msg : uint32_t {
    // DLL → Addon (events)
    EvtReady            = 0x01,
    EvtControlAcquired  = 0x10,
    EvtControlLost      = 0x11,
    EvtRotation         = 0x12,  // data: double (deltaInDegrees)
    EvtButtonClicked    = 0x13,
    EvtMenuItemSelected = 0x14,  // data: UTF-8 name
    EvtDebug            = 0x1F,  // data: UTF-8 string
    // Addon → DLL (commands)
    CmdAddMenuItem      = 0x20,  // data: "name\0iconName" (two null-terminated strings)
    CmdRemoveMenuItem   = 0x21,  // data: UTF-8 name
    CmdClearMenuItems   = 0x22,
    CmdShutdown         = 0x2F,
};

#pragma pack(push, 1)
struct MsgHdr { Msg type; uint32_t dataLen; };
#pragma pack(pop)

} // namespace DialTools
