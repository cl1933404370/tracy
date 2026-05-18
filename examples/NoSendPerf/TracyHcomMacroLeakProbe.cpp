#if defined(TRACY_SAVE_NO_SEND)
#error "TRACY_SAVE_NO_SEND leaked into host source"
#endif

#if defined(TRACYHCOM_ENABLE_PERFETTO)
#error "TRACYHCOM_ENABLE_PERFETTO leaked into host source"
#endif

#if defined(TRACYLITE_PERFETTO)
#error "TRACYLITE_PERFETTO leaked into host source"
#endif

int TracyHcomMacroLeakProbe()
{
    return 0;
}
