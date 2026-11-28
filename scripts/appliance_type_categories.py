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

APPLIANCE_TYPE_CATEGORY: Dict[int, Optional[str]] = {
    0: "waterheater",
    1: "laundry",
    2: "laundry",
    3: "refrigeration",
    4: "range",
    5: "range",
    6: "dishwasher",
    7: "range",
    8: "range",
    9: "range",
    10: "airconditioning",
    11: "range",
    12: "range",
    13: "range",
    14: "airconditioning",
    15: "range",
    16: "waterfilter",
    17: "range",
    18: "refrigeration",
    19: "range",
    20: "airconditioning",
    21: "waterfilter",
    22: "airconditioning",
    23: "laundry",
    24: "refrigeration",
    25: "refrigeration",
    26: "smallappliance",
    27: "smallappliance",
    28: "refrigeration",
    29: "airconditioning",
    30: "smallappliance",
    31: "airconditioning",
    32: "dishwasher",
    33: "smallappliance",
    34: "smallappliance",
    35: "airconditioning",
    36: "airconditioning",
    37: "smallappliance",
    38: "smallappliance",
    39: "range",
    40: "range",
    41: "range",
    42: "smallappliance",
    43: "smallappliance",
    44: "airconditioning",
    45: "laundry",
    46: "laundry",
    47: "range",
    48: "refrigeration",
    49: "range",
    50: "smallappliance",
    51: "refrigeration",
    52: "airconditioning",
    53: None,
    54: "range",
    55: "smallappliance",
    56: "airconditioning",
    57: "airconditioning",
    58: None,
}

UNKNOWN_APPLIANCE_TYPE = 255
