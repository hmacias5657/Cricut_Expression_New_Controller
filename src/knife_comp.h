#pragma once
#include <stdint.h>

void knifeCompReset();
void knifeMove(float x, float y, float feed, bool penDown);

bool knifeHasPendingMove();
void knifeExecutePending();
