# Appliance Type → Category Mapping (ERD 0x0008)

The appliance type reported by **ERD `0x0008`** selects which category ERD group an
appliance is polled for and which Home Assistant discovery entities it gets. The
single source of truth is `scripts/appliance_type_categories.py`
(`APPLIANCE_TYPE_CATEGORY`, `UNKNOWN_APPLIANCE_TYPE`); this page records the wire
facts and the constraints that keep the mapping honest. Do not hand-edit the
generated outputs to change a category — change the mapping and regenerate.

## Why one table

Three consumers used to hold three copies of the same fact and drifted apart:

|Consumer|What it derives from the mapping|
|---|---|
|`scripts/generate_erd_lists.py`|`applianceTypeToErdGroupTranslation[]` in `components/geappliances_bridge/erd_lists.cpp` — which `<category>Erds[]` list the polling bridge polls for that type|
|`scripts/ha_discovery/generators/generate_ha_discovery.py`|which `ha_discovery/<category>.jsonl` entities are emitted|
|`components/geappliances_bridge/`|which category entities are discovered on the device|

The category token is the lowercase JSONL stem. The same token is the name in the
generated `ha_discovery_categories[]` table that the firmware `strcmp`s, and the key
in `scripts/ha_discovery/pipeline/ha_constants.CATEGORIES` (the ERD-ID range table:
`refrigeration` 0x1000–0x1FFF, `laundry` 0x2000–0x2FFF, `dishwasher` 0x3000–0x3FFF,
`waterheater` 0x4000–0x4FFF, `range` 0x5000–0x5FFF, `airconditioning` 0x7000–0x7FFF,
`waterfilter` 0x8000–0x8FFF, `smallappliance` 0x9000–0x9FFF). A token that is not one
of those is rejected by the generator.

`None` means the type has no category-specific ERDs upstream: it gets the `common`
and `energy` entities only.

## How each assignment was decided

Checked 2026-11 against the `lib/public-appliance-api-documentation` submodule data:

1. The required-ERD ranges of the type's feature API in `appliance_api.json`. A
   feature API is keyed by appliance type, so its required ERDs say which range
   group the appliance actually implements. Wherever this disagreed with the polling
   table that shipped in `erd_lists.cpp`, the feature API wins.
2. Otherwise the polling table wins, because a discovery entity for an ERD the bridge
   never polls is unusable.

The old polling table and the old hand-written `should_process_category()` disagreed
for eleven types (4, 18, 28, 29, 30, 34, 43, 53, 54, 55, 56); the feature-API ranges
won each of those, per the precedence above.

## Coverage constraints

`scripts/generate_erd_lists.py::build_appliance_group_rows()` refuses to generate
rather than guess, because silently dropping a type is exactly how appliances stopped
getting their category ERDs:

- The keys MUST cover every value of the upstream ERD `0x0008` enum except `255`. A
  type the enum gained and the mapping does not know about raises.
- The mirror case raises too: a mapping entry for a type the upstream enum no longer
  defines, so dead entries cannot linger.
- A gap in the enum range raises — the generated table is indexed by appliance type
  and cannot skip values.
- A category token outside `CATEGORY_CPP` raises.
- `scripts/test_generate_erd_lists.py` pins each of those four failures.

`255` (`UNKNOWN_APPLIANCE_TYPE`) is the GEA broadcast / "unknown" appliance type. It
is never a real category owner: the generator drops it from the display map before
building the polling rows, and `scripts/generate_erd_lists.py` skips it when emitting
`appliance_type_map.h` because that switch's `default:` case owns it.

## The old table's off-by-one mislabeling

The old polling table's last two comments were mislabeled by one slot: they named
Hearth Pizza Oven and Sourdough Starter at indices 53 and 54, but the upstream enum
has **SoftStarter at 53** and **HearthPizzaOven at 54**. Soft Starter was never in the
table, so index 53 polled the range ERDs meant for the pizza oven and index 54 polled
the small-appliance ERDs meant for the sourdough starter. Types 55–58 were absent from
the table entirely, so those appliances were polled as common + energy + feature-bit
ERDs only.

Restored: 54 is `range` (the table's evident intent, the old discovery mapping and the
`PizzaOven` sibling all agree); 57 is `airconditioning` because ERDs 0x7051/0x7053 are
named "Air Purifier"; 58 has no upstream ERD defined for it yet; 53 has no upstream ERD
named for it and no feature API, so it is left with no category rather than inheriting
a guess. **UNVERIFIED on hardware: 53.**

## Mapping table

Rows with no note came from the polling table unchanged (precedence rule 2).

|Type|Upstream enum name|Category|Evidence|
|---|---|---|---|
|0|WaterHeater|waterheater|—|
|1|ClothesDryer|laundry|—|
|2|ClothesWasher|laundry|—|
|3|Refrigerator|refrigeration|—|
|4|Microwave|range|feature-API evidence (67 range ERDs)|
|5|Advantium|range|—|
|6|Dishwasher|dishwasher|—|
|7|Oven|range|—|
|8|ElectricRange|range|—|
|9|GasRange|range|—|
|10|ThermostatRAC|airconditioning|—|
|11|ElectricCooktop|range|—|
|12|PizzaOven|range|—|
|13|GasCooktop|range|—|
|14|SplitDFSDuctFreeSplitAC|airconditioning|—|
|15|Hood|range|—|
|16|PointOfEntryWaterFilter|waterfilter|—|
|17|InductionCooktop|range|—|
|18|DeliveryBox|refrigeration|feature-API evidence (5 refrigeration vs 2 laundry)|
|19|KitchenHubVentHood|range|—|
|20|ZonelinePTAC|airconditioning|—|
|21|WaterSoftener|waterfilter|—|
|22|PortableAC|airconditioning|—|
|23|CombinationWasherDryer|laundry|—|
|24|DualZoneWineChiller|refrigeration|—|
|25|BeverageCenter|refrigeration|—|
|26|CoffeeBrewer|smallappliance|—|
|27|OpalNuggetIceMaker|smallappliance|—|
|28|InHomeGrower|refrigeration|feature-API evidence (52 refrigeration ERDs)|
|29|Dehumidifier|airconditioning|feature-API evidence (16 airconditioning ERDs)|
|30|UnderCounterIceMaker|smallappliance|feature-API evidence (13 smallappliance ERDs)|
|31|ThroughWallAC|airconditioning|—|
|32|FPDishDrawer|dishwasher|—|
|33|EspressoCoffeeMaker|smallappliance|—|
|34|ToasterOven|smallappliance|feature-API evidence (112 small vs 6 range)|
|35|ZonelineVertical|airconditioning|—|
|36|CentralDFSDuctFreeSplitController|airconditioning|—|
|37|BLEMeshGateway|smallappliance|—|
|38|StandMixer|smallappliance|—|
|39|FisherPaykelCooktop|range|—|
|40|FPCooktopTeppanyaki|range|—|
|41|FPVentilationDowndraft|range|—|
|42|SmartPlug|smallappliance|—|
|43|Smoker|smallappliance|feature-API evidence (65 small vs 1 range)|
|44|AirHandlerVRF|airconditioning|—|
|45|FabricCareCabinetCloset|laundry|—|
|46|LaundryCenter|laundry|—|
|47|Grill|range|—|
|48|Freezer|refrigeration|—|
|49|WarmingDrawer|range|—|
|50|VacuumSealDrawer|smallappliance|—|
|51|WineCabinet|refrigeration|—|
|52|CentralAC|airconditioning|—|
|53|SoftStarter|*none*|no upstream ERD evidence, no feature API; UNVERIFIED on hardware|
|54|HearthPizzaOven|range|old polling table was off by one|
|55|SourdoughStarter|smallappliance|ERDs 0x9500, 0x9505|
|56|Thermostat|airconditioning|was missing from the polling table; feature API + old discovery mapping|
|57|AirPurifier|airconditioning|ERDs 0x7051, 0x7053|
|58|HomeVentilation|*none*|no upstream ERD defined yet|
|255|Unknown / GEA broadcast|*never a category owner*|skipped by both generators|

## Notes

### The category-token contract

`scripts/generate_erd_lists.py::CATEGORY_INFO` is the list that defines the category set. Each row is `(category_key, array_name, count_name, description)`, where `category_key` is a camelCase C identifier (`dishWasher`, `waterHeater`, `airConditioning`, `smallAppliance`, `waterFilter`) and **the discovery token is `category_key.lower()`** — lowercasing is the only transformation between the C symbol and every other spelling of the category (`dishWasherErds` ↔ `dishwasher.jsonl`). `CATEGORY_CPP` is derived from `CATEGORY_INFO` as `{key.lower(): (array_name, count_name)}`; it is never maintained by hand, and it is what the generator validates a mapping token against.

One token, four places that must agree:

|Place|Form|
|---|---|
|`ha_discovery/<token>.jsonl`|the file stem|
|`scripts/ha_discovery/pipeline/ha_constants.CATEGORIES`|the dict key, with the ERD-ID range|
|`scripts/appliance_type_categories.py`|the mapping value (`None` for no category)|
|generated firmware tables|`ha_discovery_categories[].name` and `applianceTypeToErdGroupTranslation[].category`, the strings `should_process_category()` in `appliance_type_category.h` `strcmp`s|

Adding a category means one `CATEGORY_INFO` row, one `CATEGORIES` range entry, a generator that emits the JSONL, and mapping values — nothing else. `common` and `energy` are tokens in the same list but are not type categories: `should_process_category()` returns true for them for every appliance type. On the polling side they are appended (`commonErds`, `energyErds`, plus the hard-coded `applianceApiFeatureErds`) only in the *poll mode without API parsing* branch; with API parsing on, the feature-bit valid-ERD set is what carries them.

### How the category group reaches the poll list

A type with no category is generated as `{ nullptr, 0, nullptr }` — today types 53 (Soft Starter) and 58 (Home Ventilation). `build_erd_poll_list()` in `erd_poll_list_builder.cpp` tests `group.erdList != nullptr` and skips the group otherwise, so such an appliance is polled for the common, energy and appliance-API feature-bit ERDs only; `should_process_category()` gives the same answer on the discovery side because the row's `category` is `NULL`.

The per-type category list is consulted only in the *poll mode without API parsing* branch, and only when `config.appliance_type < maximumApplianceType` — the same bound `should_process_category()` applies. With `appliance_api_parsing` on, the poll list is the feature-bit valid ERDs plus custom ERDs and the category list is not used at all; the token still decides which Home Assistant discovery entities are published.

