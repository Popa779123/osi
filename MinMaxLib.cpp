#include "MinMaxLib.h"
#include <windows.h>

extern "C" MINMAXLIB_API void CalculateMinMax(const int* arr, int size, int* min_val, int* max_val) {
    if (size <= 0 || arr == nullptr) return;

    int min = arr[0];
    int max = arr[0];

    for (int i = 1; i < size; ++i) {
        if (arr[i] < min) min = arr[i];
        if (arr[i] > max) max = arr[i];
        Sleep(7);
    }

    *min_val = min;
    *max_val = max;
}