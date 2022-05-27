#pragma once

HANDLE micStart(void);
void micEnd(HANDLE token);

void micOn(void);
void micOff(void);
void micVolume(float volume);

void echoOn(void);
void echoOff(void);
