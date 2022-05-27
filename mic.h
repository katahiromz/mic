#pragma once

HANDLE micStart(BOOL bMicOn);
void micEnd(HANDLE token);

void micOn(void);
void micOff(void);
void micVolume(float volume);

void micEchoOn(void);
void micEchoOff(void);
