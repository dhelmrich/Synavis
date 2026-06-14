#pragma once

#include "CoreMinimal.h"

enum class EAdiosConnectionState : uint8
{
  Idle,
  Connecting,
  Connected,
  Failed
};