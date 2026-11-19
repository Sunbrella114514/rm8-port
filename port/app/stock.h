// Stock market (stock.cpp): stock.bin stocks, daily prices (Stock_UpdatePrices @0x4b9a00, docs/gameplay_algorithms.md 4),
// holdings per role, trading (stock events 0x5E..0x61, stockBoard.ui), stock events from cards / news. docs/stock_port.md.
#pragma once
#include "turn.h"

// ---- game hooks
// [A] once per map load (rm8_app.cpp loadMap): the stock list scenes/stocks.txt (scripts/export_stocks.py) and the map folder name
// ("01".."11"; others have no map-specific stocks). The list is chosen by the next rm8StockNewGame.
void rm8StockSetMap(const std::string &scenesRoot, const std::string &mapDir);
// [V] Stock_LoadFromINI @0x4b8e20: the stocks of this map (c == map id) + the universal ones (c == 100), 16 random ones listed, the rest reserve;
// then Stock_InitPrices @0x4b9450 (300 price updates). Called by rm8GameNewGame (seat count)
void rm8StockNewGame(int seats);
// [V] Game_OnTurnStart @0x4a3b50 -> Stock_CheckUpdateTimer @0x4b9240: on weekdays (weekday < 6) the closed-days counter counts down, else prices update
void rm8StockNewDay(int weekday);
// [V] py_Stock_Open @0x501760 / Game_UseCard @0x4a4160: the market is open (weekday < 6 and no closed days)
bool rm8StockOpen();

// ---- links
// [V] role / card events that act on stocks: Role.OnEvent(seat, 94 diamond / 95 red / 96 black card, param) from the CardPy scripts
// (Role_TriggerLandEvent -> Game_UseCard / AI_OnLandEvent 0x5E..0x60); true if handled (a human gets the stock board to pick the stock)
bool rm8StockEvent(int seat, int ev, int param);
// value of a role's stocks at today's prices: sum of ftol(shares * price) (Stock_CalculateTotalValue @0x4994c0, net worth)
int32_t rm8StockValue(int seat);	// [N] int32_t
// [V] Stock_SellAll @0x4992c0: every holding goes back to the market. Returns the money value (ftol, summed); the caller decides who is paid
// (bankruptcy @0x48f500: nobody; AI_StockTrading @0x4f9310 / News_EventStockLiquidate @0x4b64e0: the deposit)
int32_t rm8StockSellAll(int seat);	// [N] int32_t
// [V] AI_StockTrading @0x4f9310: the computer's stock step of Role_StayOrBeforeRoll(1); true if it did something (the caller queues 0x25(2))
bool rm8StockAiTrade(int seat);
// [V] Game_DispatchCardAction(0x61) via gmaindlg button 7: the human opens the stock board (turn state 6); false if it did not open
bool rm8StockOpenFromHud(int seat);
// every frame (hud.cpp, before the UI renders): board / sub dialogs, test hooks
void rm8StockTick();
bool rm8StockUiOpen();	// a stock dialog covers the board (hud.cpp skips the number sprites, they are drawn after the UIs)
// news hooks (news.cpp): News_EventStockBoom / Crash @0x4b6320 / 0x4b63a0 (+-10% for 1 day), News_EventNetworkOutage @0x4b6420,
// News_EventStockLiquidate @0x4b64e0 (every role sells everything for its deposit; returns the total)
void rm8StockMarketEvent(float pct, int days);
void rm8StockCloseMarket(int days);
int rm8StockClosedDays();	// the market's closed-days counter (+0x30)
int32_t rm8StockLiquidateAll();	// [N] int32_t
// magic house effects (event_magic.cpp, MagicHouse_ApplyMagicEffect @0x4cdb10):
// effects 4 / 5: Util_Apply2DTransform @0x4d3900 sets every held stock's forced change (+0x35C = pct, +0x360 = days) with Obj_SetFields35C_360 @0x4bc620
void rm8StockForceHoldings(int seat, float pct, int days);
// effect 17: Util_ApplyScoreChange @0x4d3720: every holding gives half of its shares back to the market
void rm8StockHalveHoldings(int seat);
// effect 16 / message 0x27 kind 4 (Role_SwapAssetsWithRole @0x48bf70): the two roles swap their whole holdings
void rm8StockSwapHoldings(int a, int b);

// ---- Python (python.cpp, Stock module and the stock functions of Role)
int rm8StockCount();				// [V] py_Stock_Sum @0x5015b0: listed stocks (indices 0..n-1 are the slots)
int32_t rm8StockFlow(int i);			// [N] [V] py_Stock_FlowSum @0x501550: shares left on the market
double rm8StockCur(int i);			// [V] py_Stock_CurPrice @0x5015e0
double rm8StockAvr(int i);			// [V] py_Stock_AvrPrice @0x501650: mean of the 144 history prices
bool rm8StockTradeEnable(int i);		// [V] py_Stock_TradeEnable @0x5016f0: class > 0
int rm8StockHoldCount(int seat);		// [V] py_Role_StockSum @0x4ffdb0: holding entries
int rm8StockHoldStock(int seat, int k);		// [V] py_Role_Stock @0x4ffca0: slot of the k-th holding, -1
double rm8StockHoldPrice(int seat, int k);	// [V] py_Role_StockPrice @0x4ffe60: price of the last purchase
std::string rm8StockHoldName(int seat, int k);	// [A] the k-th holding's stock name / share count (the playerinfo list)
int rm8StockHoldQty(int seat, int k);
int32_t rm8StockHoldValue(int seat, int k);	// [N] [V] py_Role_StockValue @0x4fff30: ftol(shares * price) of the k-th holding (k < 0: all)
