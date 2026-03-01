#ifndef paravia_h
#define paravia_h

#include <stdio.h>

// Player structure
struct Player {
    int Cathedral, Clergy, CustomsDuty, CustomsDutyRevenue, DeadSerfs;
    int Difficulty, FleeingSerfs, GrainDemand, GrainPrice, GrainReserve;
    int Harvest, IncomeTax, IncomeTaxRevenue, RatsAte;
    int Justice, JusticeRevenue, Land, Marketplaces, MarketRevenue;
    int Merchants, MillRevenue, Mills, NewSerfs, Nobles, OldTitle, Palace;
    int Rats, SalesTax, SalesTaxRevenue, Serfs, SoldierPay, Soldiers, TitleNum;
    int TransplantedSerfs, Treasury, WhichPlayer, Year, YearOfDeath;
    char City[15], Name[25], Title[15];
    float PublicWorks, LandPrice;
    int IsBankrupt, IsDead, IWon, MaleOrFemale, NewTitle, InvadeMe;
};

typedef struct Player player;

// Functions Swift will call
void InitializePlayer(player *, int, int, int, char *, int);
void GenerateHarvest(player *);
void NewLandAndGrainPrices(player *);
void GenerateIncome(player *);
void AddRevenue(player *);
int CheckNewTitle(player *);
void BuyMarket(player *);
void BuyMill(player *);
void BuyPalace(player *);
void BuyCathedral(player *);
void BuySoldiers(player *);

#endif
