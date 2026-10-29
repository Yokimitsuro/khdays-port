/* Synthetic slots also translate host arguments passed to the port callbacks. */
#define khdays_host khdays_reflow_raw_host
#include "../../compat/khdays_references.h"
#undef khdays_host
#undef KHDAYS_W
void *khdays_reflow_host(unsigned reference);
#define KHDAYS_W(reference) khdays_reflow_host((unsigned)(reference))
static __inline void *khdays_host(khdays_ref reference)
{
    return reference ? khdays_reflow_host(reference) : (void *)0;
}
