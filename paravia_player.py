"""This module defines a Santa Paravia player in Python 3"""
from random import randint, random as randfloat
from inspect import getmembers, isroutine
import unittest


class playerTesting(unittest.TestCase):
    """A Unit Test to exercise all of the functions in the class"""

    def setUp(self):
        """Create a new player for testing"""
        self.player = Player("Tom")

    def tearDown(self):
        """Remove the player after testing"""
        self.player.SeizeAssets()
        del self.player

    def testPlayer(self):
        """Perform a test"""
        self.player.GenerateHarvest()
        self.player.NewLandAndGrainPrices()
        self.player.Cathedral = 100
        self.player.Mills = 100
        self.player.Marketplaces = 100
        self.player.Palace = 10
        self.player.Serfs = 100000
        self.player.Soldiers = 1000
        self.player.PublicWorks = 10.0
        self.player.Justice = 1
        self.player.GrainReserve = 200000
        self.player.Land = 10000
        self.player.Treasury = 1000000
        self.player.GenerateIncome()
        self.player.AddRevenue()
        self.player.BuyCathedral()
        self.player.BuyMarket()
        self.player.BuyMill()
        self.player.BuyPalace()
        self.player.BuySoldiers()
        self.player.BuyLand(1000)
        self.player.BuyGrain(10000)
        self.player.AddRevenue()
        other = Player("Tim", 1)
        self.player.AttackNeighbor(other)
        self.player.ReleaseGrain(100000)
        self.player.SellGrain(1000)
        self.player.SellLand(1000)
        self.assertTrue(self.player.CheckNewTitle())


class Player(object):
    """This class contains all of the data and functions necessary for a Santa Paravia player."""

    def __init__(
        self,
        name: str = "Peppone",
        city: int = 6,
        level: int = 6,
        MorF: bool = True,
        year: int = 1399,
    ):
        """Create a new player"""
        self.CityList = [
            "Santa Paravia",
            "Fiumaccio",
            "Torricella",
            "Molinetto",
            "Fontanile",
            "Romanga",
            "Monterana",
        ]
        self.Male = [
            "Sir",
            "Baron",
            "Count",
            "Marquis",
            "Duke",
            "Grand Duke",
            "Prince",
            "* H.R.H. King",
        ]
        self.Female = [
            "Lady",
            "Baroness",
            "Countess",
            "Marquise",
            "Duchess",
            "Grand Duchess",
            "Princess",
            "* H.R.H. Queen",
        ]
        self.Levels = {
            6: "Apprentice",
            7: "Journeyman",
            8: "Master",
            9: "Grand Master",
        }
        self.HarvestDescription = {
            0: "Drought. Famine Threatens.",
            1: "Bad Weather. Poor Harvest.",
            2: "Normal Weather. Average Harvest.",
            3: "Good Weather. Fine Harvest.",
            4: "Excellent Weather. Great Harvest.",
        }
        self.JusticeDescription = {
            1: "Very Fair",
            2: "Moderate",
            3: "Harsh",
            4: "Outrageous",
        }
        self.Cathedral = 0
        self.City = self.CityList[city]
        self.Clergy = 5
        self.CustomsDuty = 25
        self.Difficulty = level + 5 if level < 6 else 0
        if self.Difficulty < 6:
            self.Difficulty = 6
        if self.Difficulty > 9:
            self.Difficulty = 9
        self.GrainPrice = 25
        self.GrainReserve = 5000
        self.GrainDemand = 0
        self.IncomeTax = 5
        self.IsBankrupt = False
        self.IsDead = False
        self.IWon = False
        self.Justice = 2
        self.Land = 10000
        self.LandPrice = 10.0
        self.MaleOrFemale = MorF
        self.Marketplaces = 0
        self.Merchants = 25
        self.Mills = 0
        self.Name = name
        self.Nobles = 4
        self.OldTitle = -1
        self.Palace = 0
        self.PublicWorks = 1.0
        self.SalesTax = 10
        self.Serfs = 2000
        self.Soldiers = 25
        self.SoldierPay = 0
        self.MarketRevenue = 0
        self.MillRevenue = 0
        self.NewSerfs = 0
        self.DeadSerfs = 0
        self.TransplantedSerfs = 0
        self.FleeingSerfs = 0
        self.InvadeMe = False
        if self.MaleOrFemale is True:
            self.Title = self.Male[0]
        else:
            self.Title = self.Female[0]
        self.TitleNum = 0
        if city == 6:
            self.Title = "Baron"
            self.Name = "Peppone"
            self.TitleNum = 1
        self.Treasury = 1000
        self.WhichPlayer = city
        self.Year = year
        self.YearOfDeath = year + 21 + randint(0, 35)
        self.JusticeRevenue = 0
        self.CustomsDutyRevenue = 0
        self.IncomeTaxRevenue = 0
        self.SalesTaxRevenue = 0
        self.Harvest = 0
        self.Rats = 0
        self.RatsAte = 0

    def toDict(self):
        """Return a dict of all internal variables"""
        __myDict__ = dict()
        for __name, __obj in getmembers(self):
            # Variables that start with __ are "private" so shouldn't be displayed.
            # We don't want to display the names of the functions in the output.
            if not isroutine(__obj) and not __name.startswith("__"):
                __myDict__[__name] = __obj
        return __myDict__

    def __repr__(self):
        """Return a string of our internal variables"""
        return str(self.toDict())

    def __str__(self):
        """This routine will be called with a str(Class) call and will return
        an str representation of all the variables defined within the class.
        The data is returned as a list of lists. The first element is the
        variable name, the second is the value. The variables are alphabetized.
        [['varname', 'value'], ['varname2', 'value2'] [...]]"""
        __myDict = self.toDict()
        __retval = []
        # Why are we sorting? So the results are always returned in the same order.
        # Dicts can return data in any order, so we sort.
        for i in sorted(__myDict):
            __retval.append([i, __myDict[i]])
        return str(__retval)

    def AddRevenue(self):
        """Add money to the player's treasury and check for bankruptcy."""
        self.Treasury += (
            self.JusticeRevenue
            + self.CustomsDutyRevenue
            + self.IncomeTaxRevenue
            + self.SalesTaxRevenue
        )
        if self.Treasury < 0:
            # Penalize deficit spending
            self.Treasury = int(float(self.Treasury) * 1.5)
        if self.Treasury < (-10000 * self.TitleNum):
            self.IsBankrupt = True

    def AttackNeighbor(self, other):
        """Attack a neighboring province and take some land."""
        if self.WhichPlayer == 7:  # This is the automated Baron account
            taken = randint(1000, 10000)
        else:
            taken = (self.Soldiers * 1000) - (self.Land / 3)
        if taken > (other.Land - 5000):
            taken = (other.Land - 5000) / 2
        self.Land += taken
        other.Land -= taken
        deadsoldiers = randint(0, 40)
        if deadsoldiers > (other.Soldiers - 15):
            deadsoldiers = other.Soldiers - 15
        other.Soldiers -= deadsoldiers
        return (self.Title, self.Name, self.City, taken, deadsoldiers)

    def BuyCathedral(self):
        """Buys a Cathedral, adds clergy and public works and deducts the money"""
        self.Cathedral += 1
        self.Clergy += randint(0, 5)
        self.Treasury -= 5000
        self.PublicWorks += 1.0

    def BuyGrain(self, howMuch: float):
        """Buys grain and deducts the money"""
        self.Treasury -= (howMuch * self.GrainPrice) / 1000
        self.GrainReserve += howMuch

    def BuyLand(self, howMuch: float):
        """Buys land and deducts the money"""
        self.Treasury -= int(float(howMuch) * self.LandPrice)
        self.Land += howMuch

    def BuyMarket(self):
        """Buys a merket, adds merchants and public works and deducts the money"""
        self.Marketplaces += 1
        self.Merchants += 5
        self.Treasury -= 1000
        self.PublicWorks += 1.0

    def BuyMill(self):
        """Buys a mill, adds public works and deducts the money."""
        self.Mills += 1
        self.Treasury -= 2000
        self.PublicWorks += 0.5

    def BuyPalace(self):
        """Constructs a palace portion, adds nobles and public works and deducts the money"""
        self.Palace += 1
        self.Nobles += randint(0, 2)
        self.Treasury -= 3000
        self.PublicWorks += 0.5

    def BuySoldiers(self):
        """Buys soldiers, deducts serfs and money"""
        self.Soldiers += 20
        self.Serfs -= 20
        self.Treasury -= 500

    def CheckNewTitle(self):
        """Has the player eared a promotion (or a demotion?)"""
        if self.IsBankrupt is True:
            self.SeizeAssets()

        def limit10(num, denom):
            """A small function to limit the result to 10 or less"""
            val = round(float(num) / float(denom))
            return val if val < 11 else 10

        Total = limit10(self.Marketplaces, 1)
        Total += limit10(self.Palace, 1)
        Total += limit10(self.Cathedral, 1)
        Total += limit10(self.Mills, 1)
        Total += limit10(self.Treasury, 5000)
        Total += limit10(self.Land, 6000)
        Total += limit10(self.Merchants, 50)
        Total += limit10(self.Nobles, 5)
        Total += limit10(self.Soldiers, 50)
        Total += limit10(self.Clergy, 10)
        Total += limit10(self.Serfs, 2000)
        Total += limit10(self.PublicWorks * 100.0, 500)
        title = (Total / self.Difficulty) - self.Justice
        if title > 7:
            title = 7
        if title < 0:
            title = 0
        self.OldTitle = self.Title
        if self.MaleOrFemale is True:
            self.Title = self.Male[title]
        else:
            self.Title = self.Female[title]
        return self.OldTitle != self.Title

    def GenerateHarvest(self):
        """Generate the harvest for a new year."""
        self.Harvest = int((randint(0, 4) + randint(0, 5)) / 2)
        self.Rats = randint(0, 50)
        self.GrainReserve = int(
            ((self.GrainReserve * 100) - (self.GrainReserve * self.Rats)) / 100
        )

    def GenerateIncome(self):
        """Determine the income from taxes and justice"""
        self.JusticeRevenue = ((self.Justice * 300) - 500) * self.TitleNum
        y = (
            150.0
            - float(self.SalesTax)
            - float(self.CustomsDuty)
            - float(self.IncomeTax)
        )
        if y < 1.0:
            y = 1.0
        y = y / 100.0
        self.CustomsDutyRevenue = (
            (self.Nobles * 180) + (self.Clergy * 75) + (self.Merchants * 20)
        ) * y
        self.CustomsDutyRevenue += int(self.PublicWorks * 100.0)
        self.CustomsDutyRevenue = int(
            float(self.CustomsDuty) / 100.0 * float(self.CustomsDutyRevenue)
        )
        self.SalesTaxRevenue = (
            (self.Nobles * 50)
            + (self.Merchants * 25)
            + (int(self.PublicWorks) * 10)
        )
        self.SalesTaxRevenue *= int(
            y * (5.0 - float(self.Justice)) * float(self.SalesTax)
        )
        self.SalesTaxRevenue /= 200
        self.IncomeTaxRevenue = int(
            (
                (self.Nobles * 250)
                + int(self.PublicWorks * 20.0)
                + int(10.0 * float(self.Justice) * float(self.Nobles) * y)
            )
            * (self.IncomeTax / 100)
        )
        return (
            self.CustomsDutyRevenue,
            self.SalesTaxRevenue,
            self.IncomeTaxRevenue,
            self.JusticeRevenue,
        )

    def NewLandAndGrainPrices(self):
        """Determine the land and grain prices for the year."""
        x = float(self.Land)
        y = float(self.Serfs) - (float(self.Mills) * 100.0) * 5.0
        if y < 0.0:
            y = 0.0
        if y < x:
            x = y
        y = float(self.GrainReserve) * 2.0
        if y < x:
            x = y
        y = float(self.Harvest - 0.5)
        h = int(x * y)
        self.GrainReserve += h
        self.GrainDemand = (
            (self.Nobles * 100)
            + (self.Cathedral * 40)
            + (self.Merchants * 30)
            + (self.Soldiers * 10)
            + (self.Serfs * 5)
        )
        self.LandPrice = (
            3.0 * float(self.Harvest)
            + float(randint(0, 5))
            + float(randint(0, 5))
            + 10.0
        ) / 10.0
        if h < 1:
            y = 2.0
        else:
            y = float(self.GrainDemand) / float(h)
            if y > 2.0:
                y = 2.0
        if y < 0.8:
            y = 0.8
        self.LandPrice *= y
        if self.LandPrice < 1.0:
            self.LandPrice = 1.0
        self.GrainPrice = int(
            (6.0 - float(self.Harvest)) * 3.0
            + (float(randint(0, 4)) + float(randint(0, 4))) * 5.0 * y * 20.0
        )
        self.RatsAte = h

    def SerfsDecomposing(self, MyScale: float):
        """Calculate the number of serfs that died over the pervious year"""
        absc = int(MyScale)
        sord = float(MyScale) - float(absc)
        self.DeadSerfs = int(
            (float(randint(0, absc)) + sord) * float(self.Serfs) / 100.0
        )
        self.Serfs -= self.DeadSerfs

    def SerfsProcreating(self, MyScale: float):
        """Calculate the number of serfs that were born over the previous year"""
        absc = int(MyScale)
        sord = float(MyScale) - float(absc)
        self.NewSerfs = int(
            (float(randint(0, absc)) + sord) * float(self.Serfs) / 100.0
        )
        self.Serfs += self.NewSerfs

    def ReleaseGrain(self, howMuch: int):
        """Feed the serfs, check for starvation, harsh justice and invasions."""
        if howMuch > (self.GrainReserve * 0.8):
            return False
        if howMuch < 0:
            return False
        self.SoldierPay = 0
        self.MarketRevenue = 0
        self.NewSerfs = 0
        self.DeadSerfs = 0
        self.TransplantedSerfs = 0
        self.FleeingSerfs = 0
        self.InvadeMe = False
        self.GrainReserve -= howMuch
        z = float(howMuch) / float(self.GrainDemand) - 1.0
        if z > 0.0:
            z /= 2.0
        if z > 0.25:
            z = z / 10.0 + 0.25
        zp = (
            50.0
            - float(self.CustomsDuty)
            - float(self.SalesTax)
            - float(self.IncomeTax)
        )
        if zp < 0.0:
            zp *= float(self.Justice)
        zp /= 10.0
        if zp > 0.0:
            zp += 3.0 - float(self.Justice)
        z += zp / 10.0
        if z > 0.5:
            z = 0.5
        if howMuch < self.GrainDemand:
            x = (float(self.GrainDemand) - float(howMuch)) / float(
                self.GrainDemand
            ) * 100.0 - 9.0
            if x > 65.0:
                x = 65.0
            if x < 0.0:
                x = 0.0
            self.SerfsProcreating(3.0)
            self.SerfsDecomposing(x + 8.0)
        else:
            self.SerfsProcreating(7.0)
            self.SerfsDecomposing(3.0)
            if (self.CustomsDuty + self.SalesTax) < 35:
                self.Merchants += randint(0, 4)
            if self.IncomeTax < randint(0, 28):
                self.Nobles += randint(0, 2)
                self.Clergy += randint(0, 3)
            if howMuch > int(float(self.GrainDemand) * 1.3):
                zp = float(self.Serfs) / 1000.0
                z = (
                    (float(howMuch) - float(self.GrainDemand))
                    / float(self.GrainDemand)
                    * 10.0
                )
                z *= zp * float(randint(0, 25))
                z += float(randint(0, 40))
                self.TransplantedSerfs = int(z)
                self.Serfs += self.TransplantedSerfs
                z = z * randfloat()
                if z > 50.0:
                    z = 50.0
                self.Merchants += int(z)
                self.Nobles += 1
                self.Clergy += 2
        if self.Justice > 2:
            JusticeRevenue = (
                self.Serfs / 100 * (self.Justice - 2) * (self.Justice - 2)
            )
            self.JusticeRevenue = randint(0, JusticeRevenue)
            self.Serfs -= self.JusticeRevenue
            self.FleeingSerfs = self.JusticeRevenue
        self.MarketRevenue = self.Marketplaces * 75
        if self.MarketRevenue > 0:
            self.Treasury += self.MarketRevenue
        self.MillRevenue = self.Mills * (55 + randint(0, 250))
        if self.MillRevenue > 0:
            self.Treasury += self.MillRevenue
        self.SoldierPay = self.Soldiers * 3
        self.Treasury -= self.SoldierPay
        if ((self.Land / 1000) > self.Soldiers) or (
            (self.Land / 500) > self.Soldiers
        ):
            self.InvadeMe = True
        return True

    def SeizeAssets(self):
        """Oh no, we're bankrupt."""
        self.Marketplaces = 0
        self.Palace = 0
        self.Cathedral = 0
        self.Mills = 0
        self.Land = 6000
        self.PublicWorks = 1.0
        self.Treasury = 100
        self.IsBankrupt = False

    def SellGrain(self, howMuch: int):
        """Sell off the amount of grain passed in."""
        if howMuch > self.GrainReserve or howMuch < 0:
            return False
        self.Treasury += howMuch * self.GrainPrice / 1000
        self.GrainReserve -= howMuch
        return True

    def SellLand(self, howMuch: int):
        """Sell off unwanted land"""
        if howMuch > self.Land or howMuch < 0:
            return False
        self.Land -= howMuch
        self.Treasury += int(float(howMuch) * self.LandPrice)
        return True


if __name__ == "__main__":
    unittest.main(verbosity=9, argv=["paravia_player.py"])
