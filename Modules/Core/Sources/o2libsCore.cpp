extern void __RegisterClass__o2libs__PlayerIdentity();
extern void __RegisterClass__o2libs__ServiceSettings();
extern void __RegisterClass__o2libs__Storage();


extern void InitializeTypeso2libsCore()
{
    __RegisterClass__o2libs__PlayerIdentity();
    __RegisterClass__o2libs__ServiceSettings();
    __RegisterClass__o2libs__Storage();
}