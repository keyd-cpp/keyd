extern "C" {

enum _Unwind_Reason_Code : int;
_Unwind_Reason_Code _Unwind_RaiseException(struct _Unwind_Exception *){}
_Unwind_Reason_Code _Unwind_ForcedUnwind(struct _Unwind_Exception *, void*, void*){}
void _Unwind_Resume (struct _Unwind_Exception *){}
void _Unwind_DeleteException (struct _Unwind_Exception *){}
unsigned long _Unwind_GetGR (struct _Unwind_Context *, int){}
void _Unwind_SetGR (struct _Unwind_Context *, int, unsigned long){}
unsigned long _Unwind_GetIP (struct _Unwind_Context *){}
unsigned long _Unwind_GetIPInfo (struct _Unwind_Context *, int *){}
void _Unwind_SetIP (struct _Unwind_Context *, unsigned long){}
unsigned long _Unwind_GetLanguageSpecificData (struct _Unwind_Context*){}
unsigned long _Unwind_GetRegionStart (struct _Unwind_Context *){}
_Unwind_Reason_Code _Unwind_Resume_or_Rethrow (struct _Unwind_Exception*){}
unsigned long _Unwind_GetBSP (struct _Unwind_Context *){}
unsigned long _Unwind_GetCFA (struct _Unwind_Context *){}
unsigned long _Unwind_GetDataRelBase (struct _Unwind_Context *){}
unsigned long _Unwind_GetTextRelBase (struct _Unwind_Context *){}
typedef _Unwind_Reason_Code (*_Unwind_Trace_Fn) (struct _Unwind_Context *, void *);
_Unwind_Reason_Code _Unwind_Backtrace (_Unwind_Trace_Fn, void *){}
void *_Unwind_FindEnclosingFunction (void *){}

void __gnu_unwind_frame(void) {}
void _Unwind_Complete(void) {}
void _Unwind_VRS_Get(void) {}
void _Unwind_VRS_Set(void) {}
void __aeabi_unwind_cpp_pr0(void) {}
void __aeabi_unwind_cpp_pr1(void) {}
}
