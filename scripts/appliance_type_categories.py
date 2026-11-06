#!/usr/bin/env python3
"""Single source of truth: appliance type (ERD 0x0008) -> appliance category.

Three consumers share one fact, and used to hold three copies of it:

  - scripts/generate_erd_lists.py       -> applianceTypeToErdGroupTranslation[]
    (which <category>Erds[] list the polling bridge polls for that type)
  - scripts/ha_discovery/generators/*   -> ha_discovery/<category>.jsonl
  - components/geappliances_bridge/     -> which category entities are discovered

The token is the lowercase JSONL stem, which is also the name in the generated
ha_discovery_categories[] table that the firmware strcmp-compares, and the key
in pipeline/ha_constants.CATEGORIES (the ERD-ID range table).

None means the type has no category-specific ERDs upstream: it gets the common
and energy entities only.

How each assignment was decided (checked 2026-11 against the submodule data):

  1. The required-ERD ranges of the type's feature API in appliance_api.json.
     A feature API is keyed by appliance type, so its required ERDs say which
     range group the appliance actually implements. Wherever this disagreed
     with the polling table that shipped in erd_lists.cpp, this wins.
  2. Otherwise the polling table wins, because a discovery entity for an ERD
     the bridge never polls is unusable.

The old polling table and the old hand-written should_process_category()
disagreed for eleven types. Feature-API ERD ranges win, per the precedence
above:

  4  Microwave            -> range           (feature API: 67 range ERDs)
  18 DeliveryBox          -> refrigeration   (feature API: 5 refrigeration vs 2 laundry)
  28 InHomeGrower         -> refrigeration   (feature API: 52 refrigeration ERDs)
  29 Dehumidifier         -> airconditioning (feature API: 16 airconditioning ERDs)
  30 UnderCounterIceMaker -> smallappliance  (feature API: 13 smallappliance ERDs)
  34 ToasterOven          -> smallappliance  (feature API: 112 small vs 6 range)
  43 Smoker               -> smallappliance  (feature API: 65 small vs 1 range)
  53 SoftStarter          -> None            (no upstream evidence, see below)
  54 HearthPizzaOven      -> range           (off-by-one below)
  55 SourdoughStarter     -> smallappliance  (feature API: 12 smallappliance ERDs)
  56 Thermostat           -> airconditioning (feature API + old discovery mapping)

Types 55-58 were absent from the polling table entirely, so those appliances
were polled as common + energy + feature-bit ERDs only. The table's last two
comments were also mislabeled by one slot: they name Hearth Pizza Oven and
Sourdough Starter at indices 53 and 54, but the upstream enum has SoftStarter
at 53 and HearthPizzaOven at 54. Soft Starter was never in the table, so index
53 polled the range ERDs meant for the pizza oven and index 54 polled the
small-appliance ERDs meant for the sourdough starter. 54 is restored to range
(the table's evident intent, the old discovery mapping and the PizzaOven
sibling all agree); 57 is airconditioning because ERDs 0x7051/0x7053 are named
"Air Purifier"; 58 has no upstream ERD defined for it yet. 53 has no upstream
ERD named for it and no feature API, so it is left with no category rather than
inheriting a guess. UNVERIFIED on hardware: 53.
"""

from typing import Dict, Optional

# Appliance type value (ERD 0x0008) -> category token, or None for no category.
# Keys must cover every value of the upstream ERD 0x0008 enum except 255
# (Unknown); scripts/generate_erd_lists.py raises if the enum gains a value
# that is not listed here, so a new appliance type can never be silently
# dropped again.
APPLIANCE_TYPE_CATEGORY: Dict[int, Optional[str]] = {
    0: "waterheater",       # WaterHeater
    1: "laundry",           # ClothesDryer
    2: "laundry",           # ClothesWasher
    3: "refrigeration",     # Refrigerator
    4: "range",             # Microwave            (feature-API evidence)
    5: "range",             # Advantium
    6: "dishwasher",        # Dishwasher
    7: "range",             # Oven
    8: "range",             # ElectricRange
    9: "range",             # GasRange
    10: "airconditioning",  # ThermostatRAC
    11: "range",            # ElectricCooktop
    12: "range",            # PizzaOven
    13: "range",            # GasCooktop
    14: "airconditioning",  # SplitDFSDuctFreeSplitAC
    15: "range",            # Hood
    16: "waterfilter",      # PointOfEntryWaterFilter
    17: "range",            # InductionCooktop
    18: "refrigeration",    # DeliveryBox
    19: "range",            # KitchenHubVentHood
    20: "airconditioning",  # ZonelinePTAC
    21: "waterfilter",      # WaterSoftener
    22: "airconditioning",  # PortableAC
    23: "laundry",          # CombinationWasherDryer
    24: "refrigeration",    # DualZoneWineChiller
    25: "refrigeration",    # BeverageCenter
    26: "smallappliance",   # CoffeeBrewer
    27: "smallappliance",   # OpalNuggetIceMaker
    28: "refrigeration",    # InHomeGrower          (feature-API evidence)
    29: "airconditioning",  # Dehumidifier          (feature-API evidence)
    30: "smallappliance",   # UnderCounterIceMaker  (feature-API evidence)
    31: "airconditioning",  # ThroughWallAC
    32: "dishwasher",       # FPDishDrawer
    33: "smallappliance",   # EspressoCoffeeMaker
    34: "smallappliance",   # ToasterOven           (feature-API evidence)
    35: "airconditioning",  # ZonelineVertical
    36: "airconditioning",  # CentralDFSDuctFreeSplitController
    37: "smallappliance",   # BLEMeshGateway
    38: "smallappliance",   # StandMixer
    39: "range",            # FisherPaykelCooktop
    40: "range",            # FPCooktopTeppanyaki
    41: "range",            # FPVentilationDowndraft
    42: "smallappliance",   # SmartPlug
    43: "smallappliance",   # Smoker                (feature-API evidence)
    44: "airconditioning",  # AirHandlerVRF
    45: "laundry",          # FabricCareCabinetCloset
    46: "laundry",          # LaundryCenter
    47: "range",            # Grill
    48: "refrigeration",    # Freezer
    49: "range",            # WarmingDrawer
    50: "smallappliance",   # VacuumSealDrawer
    51: "refrigeration",    # WineCabinet
    52: "airconditioning",  # CentralAC
    53: None,               # SoftStarter           (no upstream ERD evidence)
    54: "range",            # HearthPizzaOven       (old polling table was off by one)
    55: "smallappliance",   # SourdoughStarter      (ERDs 0x9500, 0x9505)
    56: "airconditioning",  # Thermostat            (was missing from the polling table)
    57: "airconditioning",  # AirPurifier           (ERDs 0x7051, 0x7053)
    58: None,               # HomeVentilation       (no upstream ERD defined yet)
}

# GEA broadcast / "unknown" appliance type. Never a real category owner; the
# generator skips it when emitting the table.
UNKNOWN_APPLIANCE_TYPE = 255

