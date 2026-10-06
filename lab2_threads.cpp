    #include <iostream>
#include <windows.h>
#include <vector>

using namespace std;


struct ThreadData {
    vector<int>* array;
    int min_value;
    int max_value;
    double average;
};


DWORD WINAPI min_max_thread(LPVOID lpParam)
{
    ThreadData* data = static_cast<ThreadData*>(lpParam);
    vector<int>& arr = *data->array;

    data->min_value = arr[0];
    data->max_value = arr[0];

   
    for (size_t i = 1; i < arr.size(); ++i)
    {
        if (arr[i] < data->min_value)
            data->min_value = arr[i];
        Sleep(7);

        if (arr[i] > data->max_value)
            data->max_value = arr[i];
        Sleep(7);
    }

    cout << "[min_max] Min: " << data->min_value
        << ", Max: " << data->max_value << endl;

    return 0;
}


DWORD WINAPI average_thread(LPVOID lpParam)
{
    ThreadData* data = static_cast<ThreadData*>(lpParam);
    vector<int>& arr = *data->array;

   
    long long sum = 0;

   
    for (size_t i = 0; i < arr.size(); ++i)
    {
        sum += arr[i];
        Sleep(12);
    }

    data->average = static_cast<double>(sum) / arr.size();

    cout << "[average] Average: " << data->average << endl;

    return 0;
}


int main()
{
  
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);

   
    int n;
    cout << "Введите размерность массива: ";
    cin >> n;

    if (n <= 0) {
        cerr << "Ошибка: размерность должна быть положительной!" << endl;
        return 1;
    }

    
    vector<int> arr(n);
    cout << "Введите " << n << " элементов массива:" << endl;
    for (int i = 0; i < n; ++i) {
        cout << "arr[" << i << "] = ";
        cin >> arr[i];
    }

    
    ThreadData data = {};
    data.array = &arr;

    
    HANDLE hMinMax = CreateThread(NULL, 0, min_max_thread, &data, 0, NULL);
    if (hMinMax == NULL) {
        cerr << "Ошибка создания потока min_max! Код: " << GetLastError() << endl;
        return 1;
    }

    HANDLE hAverage = CreateThread(NULL, 0, average_thread, &data, 0, NULL);
    if (hAverage == NULL) {
        
        WaitForSingleObject(hMinMax, INFINITE);
        CloseHandle(hMinMax);
        cerr << "Ошибка создания потока average! Код: " << GetLastError() << endl;
        return 1;
    }

    
    WaitForSingleObject(hMinMax, INFINITE);
    WaitForSingleObject(hAverage, INFINITE);

    
    int avg_int = static_cast<int>(data.average);
    for (int i = 0; i < n; ++i) 
    {
        if (arr[i] == data.min_value || arr[i] == data.max_value)
        {
            arr[i] = avg_int;
        }
    }

    cout << "\nРезультат (min и max заменены на " << avg_int << "):" << endl;
    for (int i = 0; i < n; ++i) {
        cout << arr[i] << " ";
    }
    cout << endl;

    CloseHandle(hMinMax);
    CloseHandle(hAverage);

    return 0;
}