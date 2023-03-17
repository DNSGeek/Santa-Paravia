#!/usr/bin/env python3

from os import getenv, sep
from sys import stderr, version_info

try:
    from dearpygui import dearpygui
except ImportError:
    base = sep.join(getenv("__PYVENV_LAUNCHER__", "").split(sep)[:-1]) + sep
    stderr.write("\ndearpygui module missing. Please install using\n")
    stderr.write(f"sudo -H {base}pip{version_info.major}.{version_info.minor}")
    stderr.write(" install dearpygui\n\n")
    exit()
from random import randint

from paravia_player import Player


class SantaParavia:  # pylint: disable=too-many-ancestors
    def __init__(self):
        self.players = []
        self.Peppone = Player("Peppone", 6)
        self.name = "Santa Paravia And Fiumaccio"

    @staticmethod
    def Instructions():
        msg = ["  You are the ruler of a 15th century Italian city-state."]
        msg.append("If you rule well, you will receive higher titles. The")
        msg.append(
            "first player to become a king or queen wins. Life expectancy"
        )
        msg.append("then was brief, so you may not live long enough to win.")
        msg.append("  The computer will draw a map of your state. The size")
        msg.append("of the area in the wall grows as you buy more land. The")
        msg.append("size of the guard tower in the upper left corner shows")
        msg.append("the adequacy of your defenses. If it shrinks, equip more")
        msg.append(
            "soldiers! If the horse and plowman is touching the top wall,"
        )
        msg.append("all your land is in production. Otherwise you need more")
        msg.append("serfs, who will migrate to your state if you distribute")
        msg.append(
            "more grain than the minimum demand. If you distribute less"
        )
        msg.append("grain, some of your people will starve, and you will have")
        msg.append("a high death rate. High taxes raise money, but slow down")
        msg.append("economic growth.")
        return " ".join(msg)

    def Comparison(self, msg=""):
        for player in self.players:
            # Display comparison table
            # Player title/name, nobles, soldiers, clergy, merchants, serfs, land, treasury
            pass

    @staticmethod
    def Obituary(player):
        player.IsDead = True
        msg = "Very sad news.\n\n%s %s of %s has just died " % (
            player.Title,
            player.Name,
            player.City,
        )
        if player.Year > 1450:
            msg += "of old age after a long reign."
            return msg
        reason = randint(0, 8)
        if reason < 4:
            msg += "of pneumonia after a cold winter in a drafty castle."
        elif reason == 5:
            msg += "in a smallpox epidemic."
        elif reason == 4:
            msg += "of typhoid after drinking contaminated water."
        elif reason == 6:
            msg += "after being attacked by robbers while traveling."
        else:
            msg += "of food poinoning."
        return msg

    @staticmethod
    def Born(player):
        serfs = int((randint(0, player.Marketplaces) * player.Serfs) / 100)
        player.Serfs += serfs
        return serfs

    @staticmethod
    def Die(player):
        serfs = int((randint(0, player.Marketplaces) * player.Serfs) / 100)
        player.Serfs -= serfs
        return serfs

    def Invasion(self, player):
        for other in self.players:
            if other.WhichPlayer == player.WhichPlayer:
                # Attacking ourselves would be silly.
                continue
            if other.Soldiers < player.Soldiers:
                # Don't attack someone stronger
                continue
            if other.Soldiers < int(1.2 * (float(player.Land) / 1000.0)):
                # Don't attack if we can't take land.
                continue
            if other.Soldiers > player.Soldiers:
                return other.AttackNeighbor(player)
        # Nobody was strong enough to attack. Use Peppone
        return self.Peppone.AttackNeighbor(player)

    def ControlLoop(self):
        for player in self.players:
            player.Year += 1
            player.GenerateHaravest()
            player.NewLandAndGrainPrices()
            player.GenerateIncome()
            # Buy and Sell grain and land
            howMuch: int = 1999
            player.ReleaseGrain(howMuch)
            if player.InvadeMe:
                self.Invasion(player)
            # Adjust taxes and justice
            player.AddRevenue()
            if player.IsBankrupt:
                player.SeizeAssets()
            # Display map
            # Buy mills, markets, etc.
            if player.Year >= player.YearOfDeath:
                self.Comparison(self.Obituary(player))
                self.players.remove(player)
                continue
            player.CheckNewTitle()
            if player.TitleNum >= 7:
                self.Comparison()
                # Yay, I won!
                break


if __name__ == "__main__":
    version = dearpygui.get_dearpygui_version().split(".")
    if "b" in version[-1]:
        point = version[-1].split("b")[0]
        beta = version[-1].split("b")[1]
        version[-1] = point
        version.append(beta)
    version = [int(i) for i in version]
    print(f"Running dearpygui version {dearpygui.get_dearpygui_version()}")
    vlen = len(version) - 1
    tot = 0
    for i in version:
        tot += i * (10**vlen)
        vlen -= 1
    if tot < 112:
        stderr.write("\ndearpygui version too old.\n")
        exit()
