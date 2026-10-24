#pragma once
#ifdef _WIN32
#include "../job.h"
namespace hx::zip {
int run_zip_tune(bool quick, hx::JobControl* control=nullptr);
}
#endif
