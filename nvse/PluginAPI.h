#pragma once

#include <cstdint>

class SCRIPT_FUNCTION;
struct ExpressionEvaluatorUtils;

typedef uint32_t PluginHandle;

struct NVSEInterface {
    uint32_t nvseVersion;
    uint32_t runtimeVersion;
    uint32_t editorVersion;
    uint32_t isEditor;
    bool (*RegisterCommand)(SCRIPT_FUNCTION* info);
    void (*SetOpcodeBase)(uint32_t opcode);
    void* (*QueryInterface)(uint32_t id);
    PluginHandle (*GetPluginHandle)(void);
    bool (*RegisterTypedCommand)(SCRIPT_FUNCTION* info, uint32_t returnType);
    const char* (*GetRuntimeDirectory)();
    uint32_t isNogore;
    void (*InitExpressionEvaluatorUtils)(ExpressionEvaluatorUtils* utils);
};

struct PluginInfo {
    enum { kInfoVersion = 1 };

    uint32_t infoVersion;
    const char* name;
    uint32_t version;
};
