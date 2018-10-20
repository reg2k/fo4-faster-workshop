#pragma once
#include "rva/RVA.h"

class DataHandler;

namespace G
{
    void Init();
    extern RVA<DataHandler*> dataHandler;
}