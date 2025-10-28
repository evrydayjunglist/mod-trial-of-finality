-- Delete existing template if any, for idempotency
DELETE FROM `item_template` WHERE `entry`=90001;

-- Add new item template for the Trial Token
INSERT INTO `item_template`
(`entry`, `class`, `subclass`, `name`, `description`, `displayid`, `Quality`, `Flags`, `FlagsExtra`, `BuyCount`, `BuyPrice`, `SellPrice`, `InventoryType`, `AllowableClass`, `AllowableRace`, `ItemLevel`, `RequiredLevel`, `MaxCount`, `stackable`, `ContainerSlots`, `stat_type1`, `stat_value1`, `ScalingStatDistribution`, `DamageType`, `Delay`, `RangedModRange`, `spellid_1`, `spelltrigger_1`, `spellcharges_1`, `spellcooldown_1`, `spellcategory_1`, `spellcategorycooldown_1`, `Bonding`, `MaxDurability`, `RequiredReputationFaction`, `RequiredReputationRank`, `BagFamily`, `TotemCategory`, `ScriptName`, `DisenchantID`, `FoodType`, `minMoneyLoot`, `maxMoneyLoot`, `Duration`, `ExtraFlags`)
VALUES
(
    90001, -- entry (TrialOfFinality.TrialToken.EntryID)
    12,    -- class (ITEM_CLASS_QUEST)
    0,     -- subclass (typically 0 for quest items)
    'Trial Token', -- name
    'A token signifying participation in the perilous Trial of Finality. Loss by death is permanent.', -- description
    3869,  -- displayid (e.g., Icon for "inv_misc_coin_01", a simple gold coin. Could be changed.)
    4,     -- Quality (ITEM_QUALITY_EPIC - purple)
    16386, -- Flags (Value for ITEM_FLAG_SOULBOUND (2) + ITEM_FLAG_QUESTITEM (16384))
        -- ITEM_FLAG_SOULBOUND = 2 (dec) = 0x2 (hex)
        -- ITEM_FLAG_QUESTITEM = 16384 (dec) = 0x4000 (hex)
        -- Combined = 16386 (dec) = 0x4002 (hex)
        -- This should make it non-tradeable, non-mailable, non-sellable, non-auctionable, non-bankable (usually), and non-destroyable by player.
    0,     -- FlagsExtra (Additional flags, 0 for now)
    1,     -- BuyCount (Stack size when buying)
    0,     -- BuyPrice (Cost to buy from vendor)
    0,     -- SellPrice (Price when selling to vendor, 0 means cannot be sold)
    0,     -- InventoryType (INVTYPE_NON_EQUIP, cannot be equipped)
    -1,    -- AllowableClass (All classes)
    -1,    -- AllowableRace (All races)
    1,     -- ItemLevel
    1,     -- RequiredLevel
    1,     -- MaxCount (Player can only have one)
    1,     -- stackable (How many can stack in one slot, 1 for unique items like this)
    0,     -- ContainerSlots (Number of slots if it's a bag)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -- Stats, damage, spells - none for this token
    1,     -- Bonding (BIND_WHEN_PICKED_UP)
    0,     -- MaxDurability (0 for non-equipment)
    0,     -- RequiredReputationFaction
    0,     -- RequiredReputationRank
    0,     -- BagFamily (Mask for special bags, 0 for none)
    0,     -- TotemCategory
    '',    -- ScriptName (No item script needed for basic properties)
    0,     -- DisenchantID (Cannot be disenchanted)
    0,     -- FoodType
    0,     -- minMoneyLoot
    0,     -- maxMoneyLoot
    0,     -- Duration (0 for permanent until removed by script/event)
    0      -- ExtraFlags (0 for now, can be used for AccountBound etc. if needed later)
);
-- Notes:
-- entry = 90001 (matches config placeholder TrialOfFinality.TrialToken.EntryID)
-- class = 12 (ITEM_CLASS_QUEST) is suitable.
-- name = "Trial Token"
-- description provides flavor.
-- displayid = 3869 (inv_misc_coin_01) is a placeholder icon.
-- Quality = 4 (Epic) for importance.
-- Flags = 16386 (Soulbound + Quest Item). Quest Item flag usually handles non-destroyable, non-vendorable, non-bankable.
-- SellPrice = 0 further reinforces non-vendorable.
-- MaxCount = 1 ensures player has only one.
-- Bonding = 1 (Bind on Pickup).
-- No script needed for these flags to work.
