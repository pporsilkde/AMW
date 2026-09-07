#include "classarchetype.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include <components/esm/attr.hpp>
#include <components/esm/loadclas.hpp>
#include <components/esm/loadnpc.hpp>
#include <components/esm/loadmgef.hpp>
#include <components/esm/loadskil.hpp>

#include "creaturestats.hpp"
#include "magiceffects.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/ptr.hpp"

namespace
{
    struct Modifiers
    {
        float healthRegen = 0.f;       // Fraction of modified health per second at pair power 1.
        float magickaRegen = 0.f;      // Fraction of modified magicka per second at pair power 1.
        float fatigueRegen = 0.f;      // Additive multiplier at pair power 1.
        float moveSpeed = 0.f;         // Additive multiplier.
        float carryCapacity = 0.f;     // Additive multiplier.
        float weaponDamage = 0.f;      // Additive multiplier.
        float hitChance = 0.f;         // Flat points.
        float evasion = 0.f;           // Flat defense points.
        float spellSuccess = 0.f;      // Flat percentage points.
        float incomingDamage = 0.f;    // Additive multiplier; negative is resistance.
        float barter = 0.f;            // Fractional advantage: buy cheaper / sell dearer.
        float disposition = 0.f;        // Flat disposition points.
        float armorMovePenalty = 0.f;  // Multiplied by medium/heavy equipped load and pair power.
        float armorFatiguePenalty = 0.f;
    };

    struct Definition
    {
        int a;
        int b;
        const char* id;
        const char* nameEn;
        const char* nameRu;
        const char* perkEn;
        const char* perkRu;
        const char* drawbackEn;
        const char* drawbackRu;
        Modifiers modifiers;
    };

    // Y036: one authoritative table for all C(8,2)=28 attribute pairs. Values are
    // deliberately moderate because pair power continues growing with attributes.
    const std::array<Definition, 28> sDefinitions = {{
        { ESM::Attribute::Strength, ESM::Attribute::Intelligence, "fire_warrior", "Fire Warrior", u8"Воин огня",
          "Emberblade: Strength + Intelligence restore Magicka and add scaling Fire damage to weapon strikes.",
          u8"Пылающий клинок: Сила + Интеллект восстанавливают магию и добавляют растущий огненный урон к ударам оружием.",
          "Arcane Burden: medium/heavy armour increasingly slows movement and Fatigue recovery.",
          u8"Магическая обуза: средняя и тяжёлая броня всё сильнее замедляет движение и восстановление запаса сил.",
          { 0.f, 0.0070f, 0.f, 0.f, 0.f, 0.06f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.18f, 0.28f } },

        { ESM::Attribute::Strength, ESM::Attribute::Willpower, "crusader", "Crusader", u8"Крестоносец",
          "Zeal: Strength + Willpower improve health recovery, spell reliability and resistance to paralysis.",
          u8"Рвение: Сила + Сила воли ускоряют восстановление здоровья, повышают надёжность заклинаний и сопротивление параличу.",
          "Heavy Doctrine: raw mobility is reduced as the pair grows.",
          u8"Тяжёлая доктрина: с ростом пары снижается общая подвижность.",
          { 0.0025f, 0.f, 0.f, -0.055f, 0.f, 0.f, 0.f, 0.f, 8.f, 0.f, 0.f, 0.f, 0.f, 0.f } },

        { ESM::Attribute::Strength, ESM::Attribute::Agility, "weaponmaster", "Weaponmaster", u8"Мастер оружия",
          "Perfect Form: Strength + Agility increase weapon damage and accuracy.",
          u8"Идеальная техника: Сила + Ловкость повышают урон оружием и точность.",
          "Committed Attack: the offensive stance makes incoming hits more dangerous.",
          u8"Решительная атака: наступательная манера делает входящие удары опаснее.",
          { 0.f, 0.f, 0.f, 0.f, 0.f, 0.10f, 7.f, 0.f, 0.f, 0.06f, 0.f, 0.f, 0.f, 0.f } },

        { ESM::Attribute::Strength, ESM::Attribute::Speed, "barbarian", "Barbarian", u8"Варвар",
          "Momentum: Strength + Speed grant exceptional melee power and movement speed.",
          u8"Напор: Сила + Скорость дают высокий урон оружием и скорость передвижения.",
          "Impatience: spellcasting becomes less reliable.",
          u8"Нетерпение: заклинания становятся менее надёжными.",
          { 0.f, 0.f, 0.f, 0.08f, 0.f, 0.12f, 0.f, 0.f, -10.f, 0.f, 0.f, 0.f, 0.f, 0.f } },

        { ESM::Attribute::Strength, ESM::Attribute::Endurance, "warrior", "Warrior", u8"Воин",
          "Bulwark: Strength + Endurance increase carrying capacity and reduce incoming damage.",
          u8"Оплот: Сила + Выносливость повышают грузоподъёмность и снижают входящий урон.",
          "Weight of Steel: movement speed is slightly reduced.",
          u8"Вес стали: скорость передвижения немного снижена.",
          { 0.f, 0.f, 0.f, -0.045f, 0.18f, 0.f, 0.f, 0.f, 0.f, -0.08f, 0.f, 0.f, 0.f, 0.f } },

        { ESM::Attribute::Strength, ESM::Attribute::Personality, "knight", "Knight", u8"Рыцарь",
          "Commanding Presence: Strength + Personality improve weapon damage and disposition.",
          u8"Властное присутствие: Сила + Привлекательность повышают урон оружием и отношение окружающих.",
          "Ceremonial Burden: Fatigue recovers more slowly.",
          u8"Церемониальная ноша: запас сил восстанавливается медленнее.",
          { 0.f, 0.f, -0.16f, 0.f, 0.f, 0.06f, 0.f, 0.f, 0.f, 0.f, 0.f, 9.f, 0.f, 0.f } },

        { ESM::Attribute::Strength, ESM::Attribute::Luck, "champion", "Champion", u8"Чемпион",
          "Fortune Favours the Bold: Strength + Luck improve damage, accuracy and resistance to paralysis.",
          u8"Удача любит смелых: Сила + Удача повышают урон, точность и сопротивление параличу.",
          "Glory Seeker: heavy/medium armour increasingly limits mobility.",
          u8"Искатель славы: средняя и тяжёлая броня всё сильнее ограничивает подвижность.",
          { 0.f, 0.f, 0.f, 0.f, 0.f, 0.075f, 8.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.15f, 0.f } },

        { ESM::Attribute::Intelligence, ESM::Attribute::Willpower, "mage", "Mage", u8"Маг",
          "Deep Reservoir: Intelligence + Willpower strongly restore Magicka and improve casting.",
          u8"Глубокий резерв: Интеллект + Сила воли заметно восстанавливают магию и улучшают колдовство.",
          "Arcane Frailty: physical damage taken is increased.",
          u8"Магическая хрупкость: получаемый физический урон увеличен.",
          { 0.f, 0.0080f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 12.f, 0.08f, 0.f, 0.f, 0.f, 0.f } },

        { ESM::Attribute::Intelligence, ESM::Attribute::Agility, "witchhunter", "Witchhunter", u8"Охотник на ведьм",
          "Measured Hunt: Intelligence + Agility improve casting and accuracy and reveal nearby enchantments.",
          u8"Расчётливая охота: Интеллект + Ловкость улучшают магию и точность и позволяют чувствовать зачарования поблизости.",
          "Constant Vigilance: Fatigue recovery is reduced.",
          u8"Постоянная настороженность: восстановление запаса сил снижено.",
          { 0.f, 0.f, -0.15f, 0.f, 0.f, 0.f, 8.f, 0.f, 8.f, 0.f, 0.f, 0.f, 0.f, 0.f } },

        { ESM::Attribute::Intelligence, ESM::Attribute::Speed, "nightblade", "Nightblade", u8"Ночной клинок",
          "Shadow Veil: Intelligence + Speed improve mobility and casting; Sneak grants Night Eye and mild Chameleon.",
          u8"Теневая вуаль: Интеллект + Скорость улучшают подвижность и магию; скрытность даёт Ночное зрение и слабый Хамелеон.",
          "Glass Edge: incoming damage is increased.",
          u8"Стеклянное лезвие: входящий урон увеличен.",
          { 0.f, 0.f, 0.f, 0.10f, 0.f, 0.f, 0.f, 0.f, 8.f, 0.06f, 0.f, 0.f, 0.f, 0.f } },

        { ESM::Attribute::Intelligence, ESM::Attribute::Endurance, "sorcerer", "Sorcerer", u8"Чародей",
          "Bound Reserve: Intelligence + Endurance restore Magicka and provide scaling Spell Absorption.",
          u8"Связанный резерв: Интеллект + Выносливость восстанавливают магию и дают растущее Поглощение заклинаний.",
          "Deliberate Pace: movement speed is reduced.",
          u8"Размеренный шаг: скорость передвижения снижена.",
          { 0.f, 0.0065f, 0.f, -0.065f, 0.f, 0.f, 0.f, 0.f, 0.f, -0.05f, 0.f, 0.f, 0.f, 0.f } },

        { ESM::Attribute::Intelligence, ESM::Attribute::Personality, "bard", "Bard", u8"Бард",
          "Silver Tongue: Intelligence + Personality improve Magicka recovery, barter and disposition.",
          u8"Серебряный язык: Интеллект + Привлекательность улучшают восстановление магии, торговлю и отношение.",
          "Soft Hands: weapon damage is reduced.",
          u8"Мягкие руки: урон оружием снижен.",
          { 0.f, 0.0040f, 0.f, 0.f, 0.f, -0.06f, 0.f, 0.f, 0.f, 0.f, 0.10f, 8.f, 0.f, 0.f } },

        { ESM::Attribute::Intelligence, ESM::Attribute::Luck, "artificer", "Artificer", u8"Артефактор",
          "Inspired Design: Intelligence + Luck improve casting, carrying, trade and detection of enchantments.",
          u8"Вдохновенная конструкция: Интеллект + Удача улучшают магию, переноску, торговлю и обнаружение зачарований.",
          "Overprepared: movement speed is reduced by carried complexity.",
          u8"Избыточная подготовка: сложное снаряжение снижает скорость движения.",
          { 0.f, 0.f, 0.f, -0.055f, 0.10f, 0.f, 0.f, 0.f, 9.f, 0.f, 0.07f, 0.f, 0.f, 0.f } },

        { ESM::Attribute::Willpower, ESM::Attribute::Agility, "monk", "Monk", u8"Монах",
          "Centered Motion: Willpower + Agility improve evasion and Fatigue; without medium/heavy armour they also grant Sanctuary.",
          u8"Сосредоточенное движение: Сила воли + Ловкость повышают уклонение и восстановление сил; без средней/тяжёлой брони дают Святилище.",
          "Unburdened Discipline: medium/heavy armour sharply reduces movement.",
          u8"Дисциплина без брони: средняя и тяжёлая броня заметно снижает скорость движения.",
          { 0.f, 0.f, 0.32f, 0.f, 0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 0.f, 0.f, 0.22f, 0.10f } },

        { ESM::Attribute::Willpower, ESM::Attribute::Speed, "spellsword", "Spellsword", u8"Клинок заклятий",
          "Flow State: Willpower + Speed improve movement and spell reliability.",
          u8"Поток: Сила воли + Скорость повышают движение и надёжность заклинаний.",
          "Armoured Casting: medium/heavy armour increasingly slows Fatigue recovery.",
          u8"Колдовство в броне: средняя и тяжёлая броня всё сильнее замедляет восстановление запаса сил.",
          { 0.f, 0.f, 0.f, 0.085f, 0.f, 0.f, 0.f, 0.f, 8.f, 0.f, 0.f, 0.f, 0.f, 0.24f } },

        { ESM::Attribute::Willpower, ESM::Attribute::Endurance, "templar", "Templar", u8"Храмовник",
          "Sacred Fortitude: Willpower + Endurance restore health, improve casting, reduce damage and resist Magicka.",
          u8"Священная стойкость: Сила воли + Выносливость восстанавливают здоровье, улучшают магию, снижают урон и дают сопротивление магии.",
          "Steadfast: movement speed is reduced.",
          u8"Непоколебимость: скорость передвижения снижена.",
          { 0.0020f, 0.f, 0.f, -0.055f, 0.f, 0.f, 0.f, 0.f, 6.f, -0.05f, 0.f, 0.f, 0.f, 0.f } },

        { ESM::Attribute::Willpower, ESM::Attribute::Personality, "healer", "Healer", u8"Целитель",
          "Merciful Spirit: Willpower + Personality restore Health/Magicka, improve disposition and resist disease.",
          u8"Милосердный дух: Сила воли + Привлекательность восстанавливают здоровье/магию, улучшают отношение и сопротивление болезням.",
          "Pacifist Habit: weapon damage is reduced.",
          u8"Привычка к миру: урон оружием снижен.",
          { 0.0030f, 0.0045f, 0.f, 0.f, 0.f, -0.08f, 0.f, 0.f, 0.f, 0.f, 0.f, 7.f, 0.f, 0.f } },

        { ESM::Attribute::Willpower, ESM::Attribute::Luck, "oracle", "Oracle", u8"Провидец",
          "Premonition: Willpower + Luck improve casting/evasion and reveal nearby creatures and keys.",
          u8"Предчувствие: Сила воли + Удача улучшают магию/уклонение и позволяют чувствовать существ и ключи поблизости.",
          "Worldly Detachment: carrying capacity is reduced.",
          u8"Отрешённость от мирского: грузоподъёмность снижена.",
          { 0.f, 0.f, 0.f, 0.f, -0.12f, 0.f, 0.f, 7.f, 11.f, 0.f, 0.f, 0.f, 0.f, 0.f } },

        { ESM::Attribute::Agility, ESM::Attribute::Speed, "thief", "Thief", u8"Вор",
          "Shadow Step: Agility + Speed improve movement/evasion; while Sneaking, scaling Chameleon conceals the thief.",
          u8"Шаг в тени: Ловкость + Скорость улучшают движение/уклонение; в скрытности растущий Хамелеон скрывает вора.",
          "No Room for Error: incoming damage is increased.",
          u8"Нет права на ошибку: входящий урон увеличен.",
          { 0.f, 0.f, 0.f, 0.12f, 0.f, 0.f, 0.f, 10.f, 0.f, 0.07f, 0.f, 0.f, 0.f, 0.f } },

        { ESM::Attribute::Agility, ESM::Attribute::Endurance, "scout", "Scout", u8"Разведчик",
          "Fieldcraft: Agility + Endurance improve mobility/Fatigue/toughness and reveal nearby creatures.",
          u8"Походная выучка: Ловкость + Выносливость улучшают движение/силы/стойкость и позволяют чувствовать существ поблизости.",
          "Practical Mind: spell reliability is reduced.",
          u8"Практичный ум: надёжность заклинаний снижена.",
          { 0.f, 0.f, 0.24f, 0.075f, 0.f, 0.f, 0.f, 0.f, -6.f, -0.035f, 0.f, 0.f, 0.f, 0.f } },

        { ESM::Attribute::Agility, ESM::Attribute::Personality, "agent", "Agent", u8"Агент",
          "Social Footwork: Agility + Personality improve evasion, barter and disposition.",
          u8"Социальная пластика: Ловкость + Привлекательность улучшают уклонение, торговлю и отношение.",
          "Indirect Fighter: weapon damage is reduced.",
          u8"Непрямой бой: урон оружием снижен.",
          { 0.f, 0.f, 0.f, 0.f, 0.f, -0.05f, 0.f, 7.f, 0.f, 0.f, 0.08f, 9.f, 0.f, 0.f } },

        { ESM::Attribute::Agility, ESM::Attribute::Luck, "trickster", "Trickster", u8"Трикстер",
          "Impossible Angle: Agility + Luck improve accuracy and evasion.",
          u8"Невозможный угол: Ловкость + Удача повышают точность и уклонение.",
          "Risk Taker: incoming damage is increased.",
          u8"Любитель риска: входящий урон увеличен.",
          { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 10.f, 10.f, 0.f, 0.06f, 0.f, 0.f, 0.f, 0.f } },

        { ESM::Attribute::Speed, ESM::Attribute::Endurance, "ranger", "Ranger", u8"Следопыт",
          "Long March: Speed + Endurance improve movement/Fatigue/health recovery and reveal nearby creatures.",
          u8"Долгий марш: Скорость + Выносливость улучшают движение/силы/реген здоровья и позволяют чувствовать существ поблизости.",
          "Travel Light: carrying capacity is reduced.",
          u8"Идти налегке: грузоподъёмность снижена.",
          { 0.0015f, 0.f, 0.25f, 0.09f, -0.08f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f } },

        { ESM::Attribute::Speed, ESM::Attribute::Personality, "rogue", "Rogue", u8"Плут",
          "Quick Deal: Speed + Personality improve movement/social skills; Sneak grants a light Chameleon veil.",
          u8"Быстрая сделка: Скорость + Привлекательность улучшают движение и общение; скрытность даёт лёгкую вуаль Хамелеона.",
          "Thin Defences: incoming damage is increased.",
          u8"Слабая защита: входящий урон увеличен.",
          { 0.f, 0.f, 0.f, 0.08f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.05f, 0.10f, 6.f, 0.f, 0.f } },

        { ESM::Attribute::Speed, ESM::Attribute::Luck, "duelist", "Duelist", u8"Дуэлянт",
          "Lucky Tempo: Speed + Luck improve movement, accuracy and weapon damage.",
          u8"Счастливый темп: Скорость + Удача повышают движение, точность и урон оружием.",
          "Freedom of Motion: medium/heavy armour increasingly slows movement.",
          u8"Свобода движений: средняя и тяжёлая броня всё сильнее замедляет передвижение.",
          { 0.f, 0.f, 0.f, 0.095f, 0.f, 0.055f, 9.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.17f, 0.f } },

        { ESM::Attribute::Endurance, ESM::Attribute::Personality, "pilgrim", "Pilgrim", u8"Пилигрим",
          "Trusted Traveller: Endurance + Personality restore health, improve carrying/trade and resist common disease.",
          u8"Надёжный путник: Выносливость + Привлекательность восстанавливают здоровье, улучшают переноску/торговлю и сопротивление болезням.",
          "Simple Faith: spell reliability is reduced.",
          u8"Простая вера: надёжность заклинаний снижена.",
          { 0.0020f, 0.f, 0.f, 0.f, 0.12f, 0.f, 0.f, 0.f, -6.f, 0.f, 0.06f, 5.f, 0.f, 0.f } },

        { ESM::Attribute::Endurance, ESM::Attribute::Luck, "survivor", "Survivor", u8"Выживший",
          "Second Wind: Endurance + Luck restore health/Fatigue, reduce damage and resist poison and disease.",
          u8"Второе дыхание: Выносливость + Удача восстанавливают здоровье/силы, уменьшают урон и дают сопротивление яду и болезням.",
          "Cautious Pace: movement speed is reduced.",
          u8"Осторожный шаг: скорость движения снижена.",
          { 0.0030f, 0.f, 0.20f, -0.055f, 0.f, 0.f, 0.f, 0.f, 0.f, -0.07f, 0.f, 0.f, 0.f, 0.f } },

        { ESM::Attribute::Personality, ESM::Attribute::Luck, "adventurer", "Adventurer", u8"Авантюрист",
          "Fortunate Generalist: Personality + Luck improve movement, accuracy, barter and disposition.",
          u8"Удачливый универсал: Привлекательность + Удача улучшают движение, точность, торговлю и отношение.",
          "Master of None: weapon damage and spell reliability are slightly reduced.",
          u8"Мастер на все руки: урон оружием и надёжность заклинаний немного снижены.",
          { 0.f, 0.f, 0.f, 0.045f, 0.f, -0.04f, 5.f, 0.f, -4.f, 0.f, 0.10f, 8.f, 0.f, 0.f } }
    }};

    const Definition* findDefinition(int a, int b)
    {
        if (a == b)
            return nullptr;
        if (a > b)
            std::swap(a, b);
        for (const Definition& definition : sDefinitions)
        {
            int da = definition.a;
            int db = definition.b;
            if (da > db)
                std::swap(da, db);
            if (da == a && db == b)
                return &definition;
        }
        return nullptr;
    }

    const Definition* findDefinition(const MWWorld::Ptr& actor)
    {
        if (actor.isEmpty() || !actor.getClass().isNpc())
            return nullptr;

        const ESM::NPC* npc = actor.get<ESM::NPC>()->mBase;
        const ESM::Class* klass = MWBase::Environment::get().getWorld()->getStore().get<ESM::Class>().search(npc->mClass);
        if (!klass)
            return nullptr;
        return findDefinition(klass->mData.mAttribute[0], klass->mData.mAttribute[1]);
    }

    float pairPowerForAttributes(const MWWorld::Ptr& actor, int attribute0, int attribute1)
    {
        if (actor.isEmpty() || !actor.getClass().isActor())
            return 0.f;

        const MWMechanics::CreatureStats& stats = actor.getClass().getCreatureStats(actor);
        // MP fix: archetype strength is character progression, not equipment power.
        // Fortify Attribute, enchanted gear, spells and other temporary modifiers must
        // not increase (or decrease) the archetype pair. Attribute damage is likewise
        // ignored here; only the permanent/base attribute values drive the archetype.
        const float a = stats.getAttribute(attribute0).getBase();
        const float b = stats.getAttribute(attribute1).getBase();
        return std::clamp((a + b) / 200.f, 0.f, 1.f);
    }

    float pairPower(const MWWorld::Ptr& actor, const Definition& definition)
    {
        return pairPowerForAttributes(actor, definition.a, definition.b);
    }

    float mediumHeavyArmorLoad(const MWWorld::Ptr& actor)
    {
        if (!actor.getClass().hasInventoryStore(actor))
            return 0.f;

        const MWWorld::InventoryStore& inventory = actor.getClass().getInventoryStore(actor);
        float load = 0.f;
        for (int slot = MWWorld::InventoryStore::Slot_Helmet; slot <= MWWorld::InventoryStore::Slot_CarriedLeft; ++slot)
        {
            MWWorld::ConstContainerStoreIterator equipped = inventory.getSlot(slot);
            if (equipped == inventory.end())
                continue;
            const int skill = equipped->getClass().getEquipmentSkill(*equipped);
            if (skill == ESM::Skill::HeavyArmor)
                load += 1.f;
            else if (skill == ESM::Skill::MediumArmor)
                load += 0.7f;
        }
        // Eight body armour slots plus an optional shield is effectively a full suit.
        return std::clamp(load / 9.f, 0.f, 1.f);
    }

    template <class Getter>
    float scaled(const MWWorld::Ptr& actor, Getter getter)
    {
        const Definition* definition = findDefinition(actor);
        if (!definition)
            return 0.f;
        return getter(definition->modifiers) * pairPower(actor, *definition);
    }
}

namespace MWMechanics
{
    namespace ClassArchetype
    {
        bool getDisplayInfo(int attribute0, int attribute1, bool russian, DisplayInfo& out)
        {
            const Definition* definition = findDefinition(attribute0, attribute1);
            if (!definition)
                return false;
            out.id = definition->id;
            out.name = russian ? definition->nameRu : definition->nameEn;
            out.perk = russian ? definition->perkRu : definition->perkEn;
            out.drawback = russian ? definition->drawbackRu : definition->drawbackEn;
            return true;
        }

        bool getDisplayInfo(const MWWorld::Ptr& actor, bool russian, DisplayInfo& out)
        {
            const Definition* definition = findDefinition(actor);
            if (!definition)
                return false;
            return getDisplayInfo(definition->a, definition->b, russian, out);
        }

        float getPairPower(const MWWorld::Ptr& actor)
        {
            const Definition* definition = findDefinition(actor);
            return definition ? pairPower(actor, *definition) : 0.f;
        }

        float getPairPower(const MWWorld::Ptr& actor, int attribute0, int attribute1)
        {
            if (!findDefinition(attribute0, attribute1))
                return 0.f;
            return pairPowerForAttributes(actor, attribute0, attribute1);
        }

        bool getRuntimeState(const MWWorld::Ptr& actor, bool sneaking, RuntimeState& out)
        {
            out = RuntimeState();
            const Definition* definition = findDefinition(actor);
            if (!definition)
                return false;

            out.attribute0 = definition->a;
            out.attribute1 = definition->b;
            out.power = pairPower(actor, *definition);
            out.armorLoad = mediumHeavyArmorLoad(actor);

            const char* id = definition->id;
            if (std::strcmp(id, "nightblade") == 0 || std::strcmp(id, "thief") == 0
                || std::strcmp(id, "rogue") == 0)
            {
                out.hasConditionalBuff = true;
                out.conditionalBuffActive = sneaking;
            }
            else if (std::strcmp(id, "monk") == 0)
            {
                out.hasConditionalBuff = true;
                out.conditionalBuffActive = out.armorLoad < 0.05f;
            }

            const Modifiers& m = definition->modifiers;
            out.hasConditionalDrawback = m.armorMovePenalty > 0.f || m.armorFatiguePenalty > 0.f;
            out.conditionalDrawbackActive = !out.hasConditionalDrawback || out.armorLoad > 0.01f;
            return true;
        }

        float getHealthRegenFractionPerSecond(const MWWorld::Ptr& actor)
        {
            return scaled(actor, [](const Modifiers& m) { return m.healthRegen; });
        }

        float getMagickaRegenFractionPerSecond(const MWWorld::Ptr& actor)
        {
            return scaled(actor, [](const Modifiers& m) { return m.magickaRegen; });
        }

        float getFatigueRegenMultiplier(const MWWorld::Ptr& actor)
        {
            const Definition* definition = findDefinition(actor);
            if (!definition)
                return 1.f;
            const float power = pairPower(actor, *definition);
            const Modifiers& m = definition->modifiers;
            const float armorPenalty = m.armorFatiguePenalty * mediumHeavyArmorLoad(actor) * power;
            return std::clamp(1.f + m.fatigueRegen * power - armorPenalty, 0.55f, 1.65f);
        }

        float getMovementSpeedMultiplier(const MWWorld::Ptr& actor)
        {
            const Definition* definition = findDefinition(actor);
            if (!definition)
                return 1.f;
            const float power = pairPower(actor, *definition);
            const Modifiers& m = definition->modifiers;
            const float armorPenalty = m.armorMovePenalty * mediumHeavyArmorLoad(actor) * power;
            return std::clamp(1.f + m.moveSpeed * power - armorPenalty, 0.70f, 1.25f);
        }

        float getCarryCapacityMultiplier(const MWWorld::Ptr& actor)
        {
            return std::clamp(1.f + scaled(actor, [](const Modifiers& m) { return m.carryCapacity; }), 0.75f, 1.35f);
        }

        float getWeaponDamageMultiplier(const MWWorld::Ptr& actor)
        {
            return std::clamp(1.f + scaled(actor, [](const Modifiers& m) { return m.weaponDamage; }), 0.80f, 1.25f);
        }

        float getHitChanceBonus(const MWWorld::Ptr& actor)
        {
            return scaled(actor, [](const Modifiers& m) { return m.hitChance; });
        }

        float getEvasionBonus(const MWWorld::Ptr& actor)
        {
            return scaled(actor, [](const Modifiers& m) { return m.evasion; });
        }

        float getSpellSuccessBonus(const MWWorld::Ptr& actor)
        {
            return scaled(actor, [](const Modifiers& m) { return m.spellSuccess; });
        }

        float getIncomingDamageMultiplier(const MWWorld::Ptr& actor)
        {
            return std::clamp(1.f + scaled(actor, [](const Modifiers& m) { return m.incomingDamage; }), 0.75f, 1.25f);
        }

        float getBarterAdvantage(const MWWorld::Ptr& actor)
        {
            return std::clamp(scaled(actor, [](const Modifiers& m) { return m.barter; }), -0.15f, 0.18f);
        }

        float getDispositionBonus(const MWWorld::Ptr& actor)
        {
            return scaled(actor, [](const Modifiers& m) { return m.disposition; });
        }

        void getPassiveMagicEffects(const MWWorld::Ptr& actor, bool sneaking, std::vector<PassiveEffect>& out)
        {
            out.clear();
            const Definition* definition = findDefinition(actor);
            if (!definition)
                return;

            const float power = pairPower(actor, *definition);
            const float armorLoad = mediumHeavyArmorLoad(actor);
            const char* id = definition->id;

            auto add = [&out](int effectId, float magnitude)
            {
                if (magnitude > 0.f)
                    out.push_back({ effectId, magnitude });
            };

            // Y038: signature mechanics are shared by players and ordinary NPCs.
            // The list is also reused by the Magic window so the UI presents the
            // exact native effects that are being injected into CreatureStats.
            if (std::strcmp(id, "fire_warrior") == 0)
                add(ESM::MagicEffect::ResistFire, 5.f + 10.f * power);
            else if (std::strcmp(id, "crusader") == 0)
                add(ESM::MagicEffect::ResistParalysis, 8.f + 12.f * power);
            else if (std::strcmp(id, "champion") == 0)
                add(ESM::MagicEffect::ResistParalysis, 5.f + 10.f * power);
            else if (std::strcmp(id, "witchhunter") == 0)
                add(ESM::MagicEffect::DetectEnchantment, 50.f + 100.f * power);
            else if (std::strcmp(id, "nightblade") == 0)
            {
                if (sneaking)
                {
                    add(ESM::MagicEffect::NightEye, 10.f + 25.f * power);
                    add(ESM::MagicEffect::Chameleon, getSneakChameleonMagnitude(actor));
                }
            }
            else if (std::strcmp(id, "sorcerer") == 0)
                add(ESM::MagicEffect::SpellAbsorption, getSpellAbsorptionChance(actor));
            else if (std::strcmp(id, "artificer") == 0)
                add(ESM::MagicEffect::DetectEnchantment, 75.f + 125.f * power);
            else if (std::strcmp(id, "monk") == 0)
            {
                if (armorLoad < 0.05f)
                    add(ESM::MagicEffect::Sanctuary, 4.f + 10.f * power);
            }
            else if (std::strcmp(id, "templar") == 0)
                add(ESM::MagicEffect::ResistMagicka, 6.f + 12.f * power);
            else if (std::strcmp(id, "healer") == 0)
            {
                add(ESM::MagicEffect::ResistCommonDisease, 10.f + 20.f * power);
                add(ESM::MagicEffect::ResistBlightDisease, 10.f + 20.f * power);
            }
            else if (std::strcmp(id, "oracle") == 0)
            {
                add(ESM::MagicEffect::DetectAnimal, 100.f + 150.f * power);
                add(ESM::MagicEffect::DetectKey, 100.f + 150.f * power);
            }
            else if (std::strcmp(id, "thief") == 0)
            {
                if (sneaking)
                    add(ESM::MagicEffect::Chameleon, getSneakChameleonMagnitude(actor));
            }
            else if (std::strcmp(id, "scout") == 0)
                add(ESM::MagicEffect::DetectAnimal, 80.f + 140.f * power);
            else if (std::strcmp(id, "trickster") == 0)
                add(ESM::MagicEffect::Sanctuary, 3.f + 9.f * power);
            else if (std::strcmp(id, "ranger") == 0)
                add(ESM::MagicEffect::DetectAnimal, 120.f + 180.f * power);
            else if (std::strcmp(id, "rogue") == 0)
            {
                if (sneaking)
                    add(ESM::MagicEffect::Chameleon, getSneakChameleonMagnitude(actor));
            }
            else if (std::strcmp(id, "pilgrim") == 0)
                add(ESM::MagicEffect::ResistCommonDisease, 15.f + 25.f * power);
            else if (std::strcmp(id, "survivor") == 0)
            {
                add(ESM::MagicEffect::ResistPoison, 15.f + 25.f * power);
                add(ESM::MagicEffect::ResistCommonDisease, 10.f + 20.f * power);
            }
        }

        void addPassiveMagicEffects(const MWWorld::Ptr& actor, bool sneaking, MagicEffects& effects)
        {
            std::vector<PassiveEffect> passiveEffects;
            getPassiveMagicEffects(actor, sneaking, passiveEffects);
            for (const PassiveEffect& passive : passiveEffects)
                effects.add(EffectKey(passive.effectId), EffectParam(passive.magnitude));
        }

        float getSneakChameleonMagnitude(const MWWorld::Ptr& actor)
        {
            const Definition* definition = findDefinition(actor);
            if (!definition)
                return 0.f;
            const float power = pairPower(actor, *definition);
            const float sneak = std::clamp(static_cast<float>(actor.getClass().getSkill(actor, ESM::Skill::Sneak)) / 100.f, 0.f, 1.f);
            if (std::strcmp(definition->id, "thief") == 0)
                return std::clamp(8.f + 22.f * power + 10.f * sneak, 0.f, 40.f);
            if (std::strcmp(definition->id, "nightblade") == 0)
                return std::clamp(4.f + 10.f * power + 6.f * sneak, 0.f, 20.f);
            if (std::strcmp(definition->id, "rogue") == 0)
                return std::clamp(3.f + 7.f * power + 4.f * sneak, 0.f, 14.f);
            return 0.f;
        }

        float getSpellAbsorptionChance(const MWWorld::Ptr& actor)
        {
            const Definition* definition = findDefinition(actor);
            if (!definition || std::strcmp(definition->id, "sorcerer") != 0)
                return 0.f;
            return std::clamp(4.f + 11.f * pairPower(actor, *definition), 0.f, 15.f);
        }

        bool getWeaponElementalBonus(const MWWorld::Ptr& actor, float physicalDamage, int& effectId, float& rawDamage)
        {
            effectId = -1;
            rawDamage = 0.f;
            if (physicalDamage <= 0.f)
                return false;

            const Definition* definition = findDefinition(actor);
            if (!definition || std::strcmp(definition->id, "fire_warrior") != 0)
                return false;

            const float power = pairPower(actor, *definition);
            effectId = ESM::MagicEffect::FireDamage;
            // Attribute-driven flat component plus a small hit-size component. The cap
            // prevents slow two-handers from turning the proc into a second full hit.
            rawDamage = std::clamp(2.f + 5.f * power + physicalDamage * (0.035f * power), 2.f, 9.f);
            return true;
        }
    }
}
