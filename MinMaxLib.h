#pragma once

#ifdef MINMAXLIB_EXPORTS
#define MINMAXLIB_API __declspec(dllexport)
#else
#define MINMAXLIB_API __declspec(dllimport)
#endif

extern "C" MINMAXLIB_API void CalculateMinMax(const int* arr, int size, int* min_val, int* max_val);