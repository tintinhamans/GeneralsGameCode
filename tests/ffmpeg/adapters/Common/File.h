#pragma once
#include "Lib/BaseType.h"
#define DEBUG_LOG(args) do {} while (false)
// Narrow test adapter for the production engine File contract.
struct File
{
    enum seekMode { START, CURRENT, END };
    virtual ~File() = default;
    virtual Int read(void *, Int) = 0;
    virtual Int seek(Int, seekMode) = 0;
    virtual Int position() = 0;
    virtual Int size() = 0;
    virtual void close() = 0;
};
