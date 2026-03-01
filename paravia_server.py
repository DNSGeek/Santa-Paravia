"""
paravia_server.py — Network broker for Santa Paravia & Fiumaccio

One game at a time, in-memory state, no persistence.

Run with:
    pip install fastapi uvicorn
    uvicorn paravia_server:app --host 0.0.0.0 --port 8765

Then connect clients with:
    ./paravia --server http://<host>:8765
"""

from __future__ import annotations

import asyncio
import random
import time
import uuid
from dataclasses import asdict, dataclass, field
from enum import StrEnum
from typing import Annotated

from fastapi import FastAPI, Header, HTTPException
from pydantic import BaseModel, Field

app = FastAPI(title="Santa Paravia Server")

# ---------------------------------------------------------------------------
# Constants mirroring paravia.c
# ---------------------------------------------------------------------------

CITY_LIST: list[str] = [
    "Santa Paravia",
    "Fiumaccio",
    "Torricella",
    "Molinetto",
    "Fontanile",
    "Romanga",
]

MALE_TITLES: list[str] = [
    "Sir",
    "Baron",
    "Count",
    "Marquis",
    "Duke",
    "Grand Duke",
    "Prince",
    "* H.R.H. King",
]
FEMALE_TITLES: list[str] = [
    "Lady",
    "Baroness",
    "Countess",
    "Marquise",
    "Duchess",
    "Grand Duchess",
    "Princess",
    "* H.R.H. Queen",
]

# ---------------------------------------------------------------------------
# Enums for stringly-typed fields
# ---------------------------------------------------------------------------


class TurnPhase(StrEnum):
    HARVEST = "harvest"
    BUY_SELL_GRAIN = "buy_sell_grain"
    RELEASE_GRAIN = "release_grain"
    ADJUST_TAX = "adjust_tax"
    PURCHASES = "purchases"


class EventType(StrEnum):
    GAME_CREATED = "game_created"
    PLAYER_READY = "player_ready"
    TURN_START = "turn_start"
    HARVEST = "harvest"
    PRICES = "prices"
    BUY_GRAIN = "buy_grain"
    SELL_GRAIN = "sell_grain"
    BUY_LAND = "buy_land"
    SELL_LAND = "sell_land"
    RELEASE_GRAIN = "release_grain"
    GRAIN_EVENT = "grain_event"
    INVASION = "invasion"
    TAX_CHANGED = "tax_changed"
    REVENUE = "revenue"
    BANKRUPT = "bankrupt"
    BUILDING_BOUGHT = "building_bought"
    TITLE_CHANGED = "title_changed"
    PLAYER_DIED = "player_died"
    TURN_END = "turn_end"
    GAME_OVER = "game_over"


class TaxType(StrEnum):
    CUSTOMS = "customs"
    SALES = "sales"
    INCOME = "income"
    JUSTICE = "justice"


class BuildingType(StrEnum):
    MARKET = "market"
    MILL = "mill"
    PALACE = "palace"
    CATHEDRAL = "cathedral"
    SOLDIERS = "soldiers"


class ActionType(StrEnum):
    BUY_GRAIN = "buy_grain"
    SELL_GRAIN = "sell_grain"
    BUY_LAND = "buy_land"
    SELL_LAND = "sell_land"
    RELEASE_GRAIN = "release_grain"
    SET_TAX = "set_tax"
    END_TAX_PHASE = "end_tax_phase"
    BUY_BUILDING = "buy_building"
    END_TURN = "end_turn"


# ---------------------------------------------------------------------------
# Typed Event model (replaces dict)
# ---------------------------------------------------------------------------


class Event(BaseModel):
    seq: int
    type: EventType
    message: str
    player_index: int | None
    state_delta: dict[str, object]
    timestamp: float


# ---------------------------------------------------------------------------
# Game state
# ---------------------------------------------------------------------------


class PlayerState:
    """Mirrors the C player struct."""

    def __init__(
        self,
        city_index: int,
        name: str,
        male: bool,
        difficulty: int,
        year: int,
    ):
        self.name: str = name
        self.male: bool = male
        self.city: str = CITY_LIST[city_index]
        self.which_player: int = city_index
        self.difficulty: int = difficulty
        self.year: int = year
        self.year_of_death: int = year + 20 + random.randint(0, 35)

        # Economy
        self.treasury: int = 1000
        self.land: int = 10000
        self.land_price: float = 10.0
        self.grain_reserve: int = 5000
        self.grain_price: int = 25
        self.grain_demand: int = 0
        self.rats_ate: int = 0
        self.harvest: int = 0
        self.rats: int = 0

        # Population
        self.serfs: int = 2000
        self.soldiers: int = 25
        self.nobles: int = 4
        self.clergy: int = 5
        self.merchants: int = 25

        # Buildings
        self.cathedral: int = 0
        self.palace: int = 0
        self.marketplaces: int = 0
        self.mills: int = 0
        self.public_works: float = 1.0

        # Tax rates
        self.customs_duty: int = 25
        self.sales_tax: int = 10
        self.income_tax: int = 5
        self.justice: int = 2  # 1=very fair … 4=outrageous

        # Revenue (computed each turn)
        self.justice_revenue: int = 0
        self.customs_duty_revenue: int = 0
        self.sales_tax_revenue: int = 0
        self.income_tax_revenue: int = 0
        self.market_revenue: int = 0
        self.mill_revenue: int = 0
        self.soldier_pay: int = 0

        # Serf events
        self.new_serfs: int = 0
        self.dead_serfs: int = 0
        self.fleeing_serfs: int = 0
        self.transplanted_serfs: int = 0

        # Status flags
        self.is_bankrupt: bool = False
        self.is_dead: bool = False
        self.i_won: bool = False
        self.invade_me: bool = False

        # Title
        self.title_num: int = 1
        self.old_title: int = 1
        self.title: str = MALE_TITLES[0] if male else FEMALE_TITLES[0]

    def change_title(self) -> None:
        titles = MALE_TITLES if self.male else FEMALE_TITLES
        self.title = titles[self.title_num]
        if self.title_num == 7:
            self.i_won = True

    def to_dict(self) -> dict[str, object]:
        return {k: v for k, v in self.__dict__.items()}


class GameState:
    """Full server-side game state for one game."""

    def __init__(self):
        self.game_id: str = str(uuid.uuid4())[:8]
        self.players: list[PlayerState] = []
        self.tokens: dict[str, int] = {}  # token → player_index
        self.num_players: int = 0
        self.difficulty: int = 1
        self.started: bool = False
        self.ready_set: set[int] = set()
        self.current_turn: int = 0  # player index
        self.turn_phase: TurnPhase = TurnPhase.BUY_SELL_GRAIN
        self.sequence: int = 0  # global event counter
        self.events: list[Event] = []  # append-only log
        self.new_event: asyncio.Event = asyncio.Event()
        self.baron: PlayerState | None = None

    def emit(
        self,
        event_type: str,
        message: str,
        state_delta: dict | None = None,
        player_index: int | None = None,
    ) -> Event:
        """Append an event to the log and wake long-poll waiters."""
        self.sequence += 1
        ev = Event(
            seq=self.sequence,
            type=event_type,
            message=message,
            player_index=player_index,
            state_delta=state_delta or {},
            timestamp=time.time(),
        )
        self.events.append(ev)
        self.new_event.set()
        self.new_event.clear()  # reset immediately so next emit is fresh
        return ev

    def events_since(self, since: int) -> list[Event]:
        return [e for e in self.events if e.seq > since]

    def active_player(self) -> PlayerState:
        return self.players[self.current_turn]

    def advance_turn(self) -> None:
        """Move to the next living player."""
        n = self.num_players
        for _ in range(n):
            self.current_turn = (self.current_turn + 1) % n
            if not self.players[self.current_turn].is_dead:
                break
        self.turn_phase = TurnPhase.HARVEST


# ---------------------------------------------------------------------------
# The single global game slot
# ---------------------------------------------------------------------------

_game: GameState | None = None


def get_game() -> GameState:
    if _game is None:
        raise HTTPException(status_code=404, detail="No game in progress")
    return _game


def get_player(game: GameState, token: str) -> tuple[int, PlayerState]:
    idx = game.tokens.get(token)
    if idx is None:
        raise HTTPException(status_code=403, detail="Invalid player token")
    return idx, game.players[idx]


def assert_your_turn(game: GameState, player_index: int) -> None:
    if game.current_turn != player_index:
        raise HTTPException(
            status_code=409,
            detail=f"Not your turn — it is {game.active_player().name}'s turn",
        )


# ---------------------------------------------------------------------------
# Game logic (mirrors paravia.c functions)
# ---------------------------------------------------------------------------


def rand_int(hi: int) -> int:
    return random.randint(0, max(0, hi))


def limit10(num: int, denom: int) -> int:
    return min(10, num // denom)


def generate_harvest(p: PlayerState) -> tuple[str, int]:
    p.harvest = (rand_int(5) + rand_int(6)) // 2
    p.rats = rand_int(50)
    p.grain_reserve = (
        (p.grain_reserve * 100) - (p.grain_reserve * p.rats)
    ) // 100
    msgs: dict[int, str] = {
        0: "Drought. Famine threatens.",
        1: "Drought. Famine threatens.",
        2: "Bad weather. Poor harvest.",
        3: "Normal weather. Average harvest.",
        4: "Good weather. Fine harvest.",
        5: "Excellent weather. Great harvest!",
    }
    return msgs.get(p.harvest, "Average harvest."), p.rats


def new_land_and_grain_prices(p: PlayerState) -> None:
    my_random: float = random.random()
    x: float = float(p.land)
    y: float = float((p.serfs - p.mills) * 100) * 5.0
    if y < 0.0:
        y = 0.0
    if y < x:
        x = y
    y = float(p.grain_reserve) * 2.0
    if y < x:
        x = y
    y = float(p.harvest) + (my_random - 0.5)
    h: int = int(x * y)
    p.grain_reserve += h

    p.grain_demand = (
        p.nobles * 100
        + p.cathedral * 40
        + p.merchants * 30
        + p.soldiers * 10
        + p.serfs * 5
    )
    p.land_price = (3.0 * p.harvest + rand_int(6) + 10.0) / 10.0

    ah: int = abs(h) if h != 0 else 1
    yd: float = float(p.grain_demand) / float(ah)
    if yd > 2.0:
        yd = 2.0
    if yd < 0.8:
        yd = 0.8

    p.land_price *= yd
    if p.land_price < 1.0:
        p.land_price = 1.0

    p.grain_price = int(
        ((6.0 - p.harvest) * 3.0 + rand_int(5) + rand_int(5)) * 4.0 * yd
    )
    p.rats_ate = h


def generate_income(p: PlayerState) -> int:
    p.justice_revenue = (p.justice * 300 - 500) * p.title_num

    y = 150.0 - p.sales_tax - p.customs_duty - p.income_tax
    if y < 1.0:
        y = 1.0
    y /= 100.0

    p.customs_duty_revenue = (
        p.nobles * 180 + p.clergy * 75 + p.merchants * 20 * int(y)
    )
    p.customs_duty_revenue += int(p.public_works * 100.0)
    p.customs_duty_revenue = int(
        p.customs_duty / 100.0 * p.customs_duty_revenue
    )

    p.sales_tax_revenue = (
        p.nobles * 50 + p.merchants * 25 + int(p.public_works * 10.0)
    )
    p.sales_tax_revenue = int(
        p.sales_tax_revenue * y * (5 - p.justice) * p.sales_tax / 200
    )

    p.income_tax_revenue = (
        p.nobles * 250
        + int(p.public_works * 20.0)
        + int(10 * p.justice * p.nobles * y)
    )
    p.income_tax_revenue = int(p.income_tax_revenue * p.income_tax / 100)

    return (
        p.customs_duty_revenue
        + p.sales_tax_revenue
        + p.income_tax_revenue
        + p.justice_revenue
    )


def add_revenue(p: PlayerState) -> None:
    p.treasury += (
        p.justice_revenue
        + p.customs_duty_revenue
        + p.income_tax_revenue
        + p.sales_tax_revenue
    )
    if p.treasury < 0:
        p.treasury = int(p.treasury * 1.5)
    if p.treasury < (-10000 * p.title_num):
        p.is_bankrupt = True


def serfs_decomposing(p: PlayerState, scale: float) -> int:
    absc: int = int(scale)
    ord_: float = scale - absc
    dead: int = int((rand_int(absc) + ord_) * p.serfs / 100.0)
    p.dead_serfs = dead
    p.serfs -= dead
    return dead


def serfs_procreating(p: PlayerState, scale: float) -> int:
    absc: int = int(scale)
    ord_: float = scale - absc
    born: int = int((rand_int(absc) + ord_) * p.serfs / 100.0)
    p.new_serfs = born
    p.serfs += born
    return born


def process_grain_release(p: PlayerState, how_much: int) -> list[str]:
    """Apply grain release logic. Returns list of event messages."""
    messages: list[str] = []
    minimum: int = p.grain_reserve // 5
    maximum: int = p.grain_reserve - minimum

    # Clamp to valid range
    how_much = max(minimum, min(maximum, how_much))
    p.soldier_pay = p.market_revenue = p.new_serfs = p.dead_serfs = 0
    p.transplanted_serfs = p.fleeing_serfs = 0
    p.invade_me = False
    p.grain_reserve -= how_much

    z: float = float(how_much) / float(max(p.grain_demand, 1)) - 1.0
    if z > 0.0:
        z /= 2.0
    if z > 0.25:
        z = z / 10.0 + 0.25

    zp: float = 50.0 - p.customs_duty - p.sales_tax - p.income_tax
    if zp < 0.0:
        zp *= p.justice
    zp /= 10.0
    if zp > 0.0:
        zp += 3.0 - p.justice
    z += zp / 10.0
    if z > 0.5:
        z = 0.5

    if how_much < (p.grain_demand - 1):
        x: float = (p.grain_demand - how_much) / max(
            p.grain_demand, 1
        ) * 100.0 - 9.0
        if x > 65.0:
            x = 65.0
        if x < 0.0:
            x = 0.0
        born = serfs_procreating(p, 3.0)
        dead = serfs_decomposing(p, x + 8.0)
        messages.append(f"{born} serfs born this year.")
        messages.append(f"{dead} serfs died this year.")
    else:
        born = serfs_procreating(p, 7.0)
        dead = serfs_decomposing(p, 3.0)
        messages.append(f"{born} serfs born this year.")
        messages.append(f"{dead} serfs died this year.")

        if (p.customs_duty + p.sales_tax) < 35:
            p.merchants += rand_int(4)
        if p.income_tax < rand_int(28):
            p.nobles += rand_int(2)
            p.clergy += rand_int(3)

        if how_much > int(p.grain_demand * 1.3):
            zp2: float = p.serfs / 1000.0
            z2: float = (
                (how_much - p.grain_demand) / max(p.grain_demand, 1) * 10.0
            )
            z2 *= zp2 * rand_int(25)
            z2 += rand_int(40)
            z2 = min(z2, 200.0)
            p.transplanted_serfs = int(z2)
            p.serfs += p.transplanted_serfs
            messages.append(f"{p.transplanted_serfs} serfs move to the city.")
            merchants_gain: int = min(int(z2 * random.random()), 50)
            p.merchants += merchants_gain
            p.nobles += 1
            p.clergy += 2

    if p.justice > 2:
        jrev: int = p.serfs // 100 * (p.justice - 2) ** 2
        jrev = rand_int(jrev) if jrev > 0 else 0
        p.fleeing_serfs = jrev
        p.serfs -= jrev
        if jrev:
            messages.append(f"{jrev} serfs flee harsh justice.")

    p.market_revenue = p.marketplaces * 75
    if p.market_revenue:
        p.treasury += p.market_revenue
        messages.append(f"Markets earned {p.market_revenue} florins.")

    p.mill_revenue = p.mills * (55 + rand_int(250))
    if p.mill_revenue:
        p.treasury += p.mill_revenue
        messages.append(f"Woolen mills earned {p.mill_revenue} florins.")

    p.soldier_pay = p.soldiers * 3
    p.treasury -= p.soldier_pay
    messages.append(f"Paid soldiers {p.soldier_pay} florins.")
    messages.append(f"{p.serfs} serfs in city.")

    return messages


def check_new_title(p: PlayerState) -> str | None:
    total = (
        limit10(p.marketplaces, 1)
        + limit10(p.palace, 1)
        + limit10(p.cathedral, 1)
        + limit10(p.mills, 1)
        + limit10(p.treasury, 5000)
        + limit10(p.land, 6000)
        + limit10(p.merchants, 50)
        + limit10(p.nobles, 5)
        + limit10(p.soldiers, 50)
        + limit10(p.clergy, 10)
        + limit10(p.serfs, 2000)
        + limit10(int(p.public_works * 100.0), 500)
    )

    p.title_num = (total // p.difficulty) - p.justice
    p.title_num = max(0, min(7, p.title_num))

    if p.title_num > p.old_title:
        p.old_title = p.title_num
        p.change_title()
        return p.title
    p.title_num = p.old_title
    return None


def attack_neighbor(
    attacker: PlayerState, victim: PlayerState
) -> tuple[int, int]:
    """Returns (land_taken, soldiers_lost)."""
    if attacker.which_player == 6:
        land_taken: int = rand_int(9000) + 1000
    else:
        land_taken = attacker.soldiers * 1000 - attacker.land // 3
    land_taken = min(land_taken, (victim.land - 5000) // 2)
    land_taken = max(0, land_taken)
    attacker.land += land_taken
    victim.land -= land_taken
    dead: int = max(0, min(rand_int(40), victim.soldiers - 15))
    victim.soldiers -= dead
    return land_taken, dead


def seize_assets(p: PlayerState) -> None:
    p.marketplaces = 0
    p.palace = 0
    p.cathedral = 0
    p.mills = 0
    p.land = 6000
    p.public_works = 1.0
    p.treasury = 100
    p.is_bankrupt = False


def death_cause(year: int) -> str:
    if year > 1450:
        return "of old age after a long reign."
    causes: list[str] = [
        "of pneumonia after a cold winter in a drafty castle.",
        "of pneumonia after a cold winter in a drafty castle.",
        "of pneumonia after a cold winter in a drafty castle.",
        "of pneumonia after a cold winter in a drafty castle.",
        "of typhoid after drinking contaminated water.",
        "in a smallpox epidemic.",
        "after being attacked by robbers while travelling.",
        "of food poisoning.",
        "of food poisoning.",
    ]
    return random.choice(causes)


# ---------------------------------------------------------------------------
# Request / Response models
# ---------------------------------------------------------------------------


class PlayerInfo(BaseModel):
    name: str = Field(max_length=24)
    male: bool = True


class NewGameRequest(BaseModel):
    players: list[PlayerInfo] = Field(min_length=1, max_length=6)
    difficulty: Annotated[int, Field(ge=1, le=4)] = 2


class GrainAmountParams(BaseModel):
    amount: int = Field(ge=0)


class LandAmountParams(BaseModel):
    amount: int = Field(ge=0)


class ReleaseGrainParams(BaseModel):
    amount: int = Field(ge=0)


class SetTaxParams(BaseModel):
    tax_type: TaxType
    value: int = Field(ge=0, le=100)


class BuyBuildingParams(BaseModel):
    building: BuildingType


class ActionRequest(BaseModel):
    type: ActionType
    params: dict[str, object] = {}

    def grain_params(self) -> GrainAmountParams:
        return GrainAmountParams.model_validate(self.params)

    def land_params(self) -> LandAmountParams:
        return LandAmountParams.model_validate(self.params)

    def release_params(self) -> ReleaseGrainParams:
        return ReleaseGrainParams.model_validate(self.params)

    def tax_params(self) -> SetTaxParams:
        return SetTaxParams.model_validate(self.params)

    def building_params(self) -> BuyBuildingParams:
        return BuyBuildingParams.model_validate(self.params)


# ---------------------------------------------------------------------------
# Response models
# ---------------------------------------------------------------------------


class NewGameResponse(BaseModel):
    game_id: str
    tokens: dict[str, str]
    num_players: int
    cities: list[str]


class ReadyResponse(BaseModel):
    ready_count: int
    started: bool


class TurnResponse(BaseModel):
    player_index: int
    player_name: str
    title: str
    city: str
    phase: TurnPhase
    sequence: int
    year: int


class LogResponse(BaseModel):
    events: list[Event]
    latest_seq: int


class ActionResponse(BaseModel):
    ok: bool
    events: list[Event]
    sequence: int
    phase: TurnPhase
    state: dict[str, object]
    game_over: bool = False


class GameSnapshot(BaseModel):
    game_id: str
    started: bool
    current_turn: int
    turn_phase: TurnPhase
    sequence: int
    players: list[dict[str, object]]
    alive: list[int]


class RootResponse(BaseModel):
    status: str
    message: str | None = None
    game_id: str | None = None
    num_players: int | None = None
    current_turn: int | None = None
    active_player: str | None = None
    phase: TurnPhase | None = None
    sequence: int | None = None


# ---------------------------------------------------------------------------
# Helper: build a full state snapshot
# ---------------------------------------------------------------------------


def snapshot(game: GameState) -> GameSnapshot:
    alive = [i for i, p in enumerate(game.players) if not p.is_dead]
    return GameSnapshot(
        game_id=game.game_id,
        started=game.started,
        current_turn=game.current_turn,
        turn_phase=game.turn_phase,
        sequence=game.sequence,
        players=[p.to_dict() for p in game.players],
        alive=alive,
    )


# ---------------------------------------------------------------------------
# Endpoints
# ---------------------------------------------------------------------------


@app.post("/game/new")
async def new_game(req: NewGameRequest) -> NewGameResponse:
    global _game

    g = GameState()
    g.difficulty = req.difficulty
    g.num_players = len(req.players)

    tokens_out: dict[str, str] = {}
    for i, pinfo in enumerate(req.players):
        p = PlayerState(i, pinfo.name, pinfo.male, req.difficulty, 1400)
        g.players.append(p)
        tok = str(uuid.uuid4())
        g.tokens[tok] = i
        tokens_out[str(i)] = tok

    # Baron NPC — always player slot 6
    g.baron = PlayerState(5, "Peppone", True, 4, 1400)
    g.baron.which_player = 6

    _game = g
    g.emit(
        EventType.GAME_CREATED,
        f"Game created with {g.num_players} players. "
        "Waiting for all players to signal ready.",
    )

    return NewGameResponse(
        game_id=g.game_id,
        tokens=tokens_out,
        num_players=g.num_players,
        cities=[p.city for p in g.players],
    )


def _begin_turn(g: GameState) -> None:
    """Server-side start-of-turn: harvest + prices, emit events."""
    p = g.active_player()
    p.year += 1

    harvest_msg, rats_pct = generate_harvest(p)
    new_land_and_grain_prices(p)

    g.emit(
        EventType.TURN_START,
        f"Year {p.year} — {p.title} {p.name}'s turn begins.",
        state_delta={"player": p.to_dict()},
        player_index=g.current_turn,
    )
    g.emit(
        EventType.HARVEST,
        f"Rats ate {rats_pct}% of grain. {harvest_msg} "
        f"({p.rats_ate} steres produced)",
        state_delta={"player": p.to_dict()},
        player_index=g.current_turn,
    )
    g.emit(
        EventType.PRICES,
        f"Grain: {p.grain_price}/1000 steres. "
        f"Land: {p.land_price:.2f}/hectare. "
        f"Grain demand: {p.grain_demand} steres.",
        state_delta={"player": p.to_dict()},
        player_index=g.current_turn,
    )
    g.turn_phase = TurnPhase.BUY_SELL_GRAIN


@app.post("/game/{game_id}/ready")
async def player_ready(
    game_id: str, x_player_token: Annotated[str, Header()]
) -> ReadyResponse:
    g = get_game()
    idx, p = get_player(g, x_player_token)
    g.ready_set.add(idx)

    g.emit(EventType.PLAYER_READY, f"{p.name} is ready.", player_index=idx)

    if len(g.ready_set) == g.num_players and not g.started:
        g.started = True
        g.current_turn = 0
        g.turn_phase = TurnPhase.HARVEST
        _begin_turn(g)

    return ReadyResponse(ready_count=len(g.ready_set), started=g.started)


@app.get("/game/{game_id}/state")
async def get_state(game_id: str) -> GameSnapshot:
    return snapshot(get_game())


@app.get("/game/{game_id}/turn")
async def get_turn(game_id: str) -> TurnResponse:
    g = get_game()
    p = g.active_player()
    return TurnResponse(
        player_index=g.current_turn,
        player_name=p.name,
        title=p.title,
        city=p.city,
        phase=g.turn_phase,
        sequence=g.sequence,
        year=p.year,
    )


@app.get("/game/{game_id}/log")
async def get_log(
    game_id: str, since: int = 0, timeout: float = 10.0
) -> LogResponse:
    """Long-poll endpoint. Returns immediately if events exist, else waits."""
    g = get_game()
    events = g.events_since(since)
    if events:
        return LogResponse(events=events, latest_seq=g.sequence)

    try:
        await asyncio.wait_for(g.new_event.wait(), timeout=timeout)
    except asyncio.TimeoutError:
        pass

    return LogResponse(events=g.events_since(since), latest_seq=g.sequence)


@app.post("/game/{game_id}/action")
async def post_action(
    game_id: str,
    req: ActionRequest,
    x_player_token: Annotated[str, Header()],
) -> ActionResponse:
    g = get_game()
    idx, p = get_player(g, x_player_token)
    assert_your_turn(g, idx)

    emitted: list[Event] = []

    def delta() -> dict[str, object]:
        return {"player": p.to_dict()}

    # ------------------------------------------------------------------
    # buy_grain
    # ------------------------------------------------------------------
    if req.type == ActionType.BUY_GRAIN:
        params = req.grain_params()
        cost = params.amount * p.grain_price // 1000
        p.treasury -= cost
        p.grain_reserve += params.amount
        emitted.append(
            g.emit(
                EventType.BUY_GRAIN,
                f"{p.name} buys {params.amount} steres of grain for {cost} florins.",
                state_delta=delta(),
                player_index=idx,
            )
        )

    # ------------------------------------------------------------------
    # sell_grain
    # ------------------------------------------------------------------
    elif req.type == ActionType.SELL_GRAIN:
        params = req.grain_params()
        if params.amount > p.grain_reserve:
            raise HTTPException(400, "Not enough grain")
        income = params.amount * p.grain_price // 1000
        p.treasury += income
        p.grain_reserve -= params.amount
        emitted.append(
            g.emit(
                EventType.SELL_GRAIN,
                f"{p.name} sells {params.amount} steres of grain for {income} florins.",
                state_delta=delta(),
                player_index=idx,
            )
        )

    # ------------------------------------------------------------------
    # buy_land
    # ------------------------------------------------------------------
    elif req.type == ActionType.BUY_LAND:
        params = req.land_params()
        cost = int(params.amount * p.land_price)
        p.land += params.amount
        p.treasury -= cost
        emitted.append(
            g.emit(
                EventType.BUY_LAND,
                f"{p.name} buys {params.amount} hectares for {cost} florins.",
                state_delta=delta(),
                player_index=idx,
            )
        )

    # ------------------------------------------------------------------
    # sell_land
    # ------------------------------------------------------------------
    elif req.type == ActionType.SELL_LAND:
        params = req.land_params()
        if params.amount > (p.land - 5000):
            raise HTTPException(400, "Cannot sell that much land")
        income = int(params.amount * p.land_price)
        p.land -= params.amount
        p.treasury += income
        emitted.append(
            g.emit(
                EventType.SELL_LAND,
                f"{p.name} sells {params.amount} hectares for {income} florins.",
                state_delta=delta(),
                player_index=idx,
            )
        )

    # ------------------------------------------------------------------
    # release_grain  (ends buy_sell phase, begins tax phase)
    # ------------------------------------------------------------------
    elif req.type == ActionType.RELEASE_GRAIN:
        if g.turn_phase not in (
            TurnPhase.BUY_SELL_GRAIN,
            TurnPhase.RELEASE_GRAIN,
        ):
            raise HTTPException(409, f"Wrong phase: {g.turn_phase}")
        params = req.release_params()
        minimum = p.grain_reserve // 5
        maximum = p.grain_reserve - minimum
        how_much = max(minimum, min(maximum, params.amount))

        messages = process_grain_release(p, how_much)
        emitted.append(
            g.emit(
                EventType.RELEASE_GRAIN,
                f"{p.name} releases {how_much} steres of grain.",
                state_delta=delta(),
                player_index=idx,
            )
        )
        for msg in messages:
            emitted.append(
                g.emit(
                    EventType.GRAIN_EVENT,
                    msg,
                    state_delta=delta(),
                    player_index=idx,
                )
            )

        if p.invade_me:
            attacker: PlayerState | None = next(
                (
                    o
                    for o in g.players
                    if o.which_player != idx
                    and not o.is_dead
                    and o.soldiers > p.soldiers * 2.4
                ),
                None,
            )
            if attacker is None:
                attacker = g.baron
            if attacker is not None:
                taken, dead_soldiers = attack_neighbor(attacker, p)
                emitted.append(
                    g.emit(
                        EventType.INVASION,
                        f"{attacker.title} {attacker.name} of {attacker.city} "
                        f"invades and seizes {taken} hectares! "
                        f"{p.name} loses {dead_soldiers} soldiers.",
                        state_delta=delta(),
                        player_index=idx,
                    )
                )

        g.turn_phase = TurnPhase.ADJUST_TAX

    # ------------------------------------------------------------------
    # set_tax
    # ------------------------------------------------------------------
    elif req.type == ActionType.SET_TAX:
        params = req.tax_params()
        match params.tax_type:
            case TaxType.CUSTOMS:
                p.customs_duty = max(0, min(100, params.value))
            case TaxType.SALES:
                p.sales_tax = max(0, min(50, params.value))
            case TaxType.INCOME:
                p.income_tax = max(0, min(25, params.value))
            case TaxType.JUSTICE:
                p.justice = max(1, min(4, params.value))

        revenues = generate_income(p)
        emitted.append(
            g.emit(
                EventType.TAX_CHANGED,
                f"{p.name} adjusts {params.tax_type} to {params.value}. "
                f"Projected revenues: {revenues} florins.",
                state_delta=delta(),
                player_index=idx,
            )
        )

    # ------------------------------------------------------------------
    # end_tax_phase  →  apply revenue, check bankruptcy, move to purchases
    # ------------------------------------------------------------------
    elif req.type == ActionType.END_TAX_PHASE:
        if g.turn_phase != TurnPhase.ADJUST_TAX:
            raise HTTPException(409, f"Wrong phase: {g.turn_phase}")
        generate_income(p)
        add_revenue(p)

        if p.is_bankrupt:
            seize_assets(p)
            emitted.append(
                g.emit(
                    EventType.BANKRUPT,
                    f"{p.title} {p.name} is bankrupt! Creditors seize assets.",
                    state_delta=delta(),
                    player_index=idx,
                )
            )
        else:
            emitted.append(
                g.emit(
                    EventType.REVENUE,
                    f"{p.name} collects revenues. Treasury: {p.treasury} florins.",
                    state_delta=delta(),
                    player_index=idx,
                )
            )
        g.turn_phase = TurnPhase.PURCHASES

    # ------------------------------------------------------------------
    # buy_building
    # ------------------------------------------------------------------
    elif req.type == ActionType.BUY_BUILDING:
        params = req.building_params()
        match params.building:
            case BuildingType.MARKET:
                p.marketplaces += 1
                p.merchants += 5
                p.treasury -= 1000
                p.public_works += 1.0
            case BuildingType.MILL:
                p.mills += 1
                p.treasury -= 2000
                p.public_works += 0.25
            case BuildingType.PALACE:
                p.palace += 1
                p.nobles += rand_int(2)
                p.treasury -= 3000
                p.public_works += 0.5
            case BuildingType.CATHEDRAL:
                p.cathedral += 1
                p.clergy += rand_int(6)
                p.treasury -= 5000
                p.public_works += 1.0
            case BuildingType.SOLDIERS:
                p.soldiers += 20
                p.serfs -= 20
                p.treasury -= 500

        emitted.append(
            g.emit(
                EventType.BUILDING_BOUGHT,
                f"{p.name} buys a {params.building}. Treasury: {p.treasury} florins.",
                state_delta=delta(),
                player_index=idx,
            )
        )

    # ------------------------------------------------------------------
    # end_turn  →  check title, check death, advance to next player
    # ------------------------------------------------------------------
    elif req.type == ActionType.END_TURN:
        if g.turn_phase != TurnPhase.PURCHASES:
            raise HTTPException(409, f"Wrong phase: {g.turn_phase}")

        new_title = check_new_title(p)
        if new_title:
            emitted.append(
                g.emit(
                    EventType.TITLE_CHANGED,
                    f"Good news! {p.name} achieves the rank of {new_title}!",
                    state_delta=delta(),
                    player_index=idx,
                )
            )

        if p.i_won:
            emitted.append(
                g.emit(
                    EventType.GAME_OVER,
                    f"GAME OVER — {p.title} {p.name} WINS!",
                    state_delta=snapshot(g).model_dump(),
                )
            )
            return ActionResponse(
                ok=True,
                events=emitted,
                sequence=g.sequence,
                phase=g.turn_phase,
                state=p.to_dict(),
                game_over=True,
            )

        if p.year >= p.year_of_death:
            p.is_dead = True
            emitted.append(
                g.emit(
                    EventType.PLAYER_DIED,
                    f"{p.title} {p.name} has died {death_cause(p.year)}",
                    state_delta=delta(),
                    player_index=idx,
                )
            )

        alive = [pl for pl in g.players if not pl.is_dead]
        if not alive:
            emitted.append(
                g.emit(
                    EventType.GAME_OVER,
                    "All rulers have perished. The game ends.",
                    state_delta=snapshot(g).model_dump(),
                )
            )
            return ActionResponse(
                ok=True,
                events=emitted,
                sequence=g.sequence,
                phase=g.turn_phase,
                state=p.to_dict(),
                game_over=True,
            )

        emitted.append(
            g.emit(
                EventType.TURN_END,
                f"{p.title} {p.name}'s turn ends.",
                state_delta=delta(),
                player_index=idx,
            )
        )
        g.advance_turn()
        _begin_turn(g)

    return ActionResponse(
        ok=True,
        events=emitted,
        sequence=g.sequence,
        phase=g.turn_phase,
        state=p.to_dict(),
    )


@app.get("/")
async def root() -> RootResponse:
    if _game is None:
        return RootResponse(status="idle", message="No game in progress.")
    p = _game.active_player() if _game.started else None
    return RootResponse(
        status="game_in_progress" if _game.started else "waiting",
        game_id=_game.game_id,
        num_players=_game.num_players,
        current_turn=_game.current_turn,
        active_player=p.name if p else None,
        phase=_game.turn_phase,
        sequence=_game.sequence,
    )
