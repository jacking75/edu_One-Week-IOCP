
#pragma once

#include "BaseContext.h"
#include "Session.h"

struct IOContext : public BaseContext
{
    SessionPtr session;

    IOContext() : BaseContext(ContextType::RECV)
    {
    }
};
