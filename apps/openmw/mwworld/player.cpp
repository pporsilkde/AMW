#include "player.hpp"

#include <stdexcept>
#include <algorithm>
#include <cmath>

#include <components/debug/debuglog.hpp>
#include <components/misc/stringops.hpp>

#include <components/esm/esmreader.hpp>
#include <components/esm/esmwriter.hpp>
#include <components/esm/player.hpp>
#include <components/esm/defs.hpp>
#include <components/esm/loadbsgn.hpp>

#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"

#include "../mwmechanics/movement.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwmechanics/spellutil.hpp"

#include "class.hpp"
#include "ptr.hpp"
#include "cellstore.hpp"

namespace MWWorld
{
    Player::Player (const ESM::NPC *player)
      : mCellStore(nullptr),
        mLastKnownExteriorPosition(0,0,0),
        mMarkedPosition(ESM::Position()),
        mMarkedCell(nullptr),
        mAutoMove(false),
        mForwardBackward(0),
        mTeleported(false),
        mCurrentCrimeId(-1),
        mPaidCrimeId(-1),
        mAttackingOrSpell(false),
        mJumping(false),
        mMount(),
        mMountLeftRight(0.f),
        mMountForwardBackward(0.f),
        mMountYawDelta(0.f),
        mMountRun(false),
        mDismountSneakLatch(false)
    {
        ESM::CellRef cellRef;
        cellRef.blank();
        cellRef.mRefID = "player";
        mPlayer = LiveCellRef<ESM::NPC>(cellRef, player);

        ESM::Position playerPos = mPlayer.mData.getPosition();
        playerPos.pos[0] = playerPos.pos[1] = playerPos.pos[2] = 0;
        mPlayer.mData.setPosition(playerPos);
    }

    void Player::saveStats()
    {
        MWMechanics::NpcStats& stats = getPlayer().getClass().getNpcStats(getPlayer());

        for (int i=0; i<ESM::Skill::Length; ++i)
            mSaveSkills[i] = stats.getSkill(i);
        for (int i=0; i<ESM::Attribute::Length; ++i)
            mSaveAttributes[i] = stats.getAttribute(i);
    }

    void Player::restoreStats()
    {
        const MWWorld::Store<ESM::GameSetting>& gmst = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>();
        MWMechanics::CreatureStats& creatureStats = getPlayer().getClass().getCreatureStats(getPlayer());
        MWMechanics::NpcStats& npcStats = getPlayer().getClass().getNpcStats(getPlayer());
        MWMechanics::DynamicStat<float> health = creatureStats.getDynamic(0);
        creatureStats.setHealth(int(health.getBase() / gmst.find("fWereWolfHealth")->mValue.getFloat()));
        for (int i=0; i<ESM::Skill::Length; ++i)
            npcStats.setSkill(i, mSaveSkills[i]);
        for (int i=0; i<ESM::Attribute::Length; ++i)
            npcStats.setAttribute(i, mSaveAttributes[i]);
    }

    void Player::setWerewolfStats()
    {
        const MWWorld::Store<ESM::GameSetting>& gmst = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>();
        MWMechanics::CreatureStats& creatureStats = getPlayer().getClass().getCreatureStats(getPlayer());
        MWMechanics::NpcStats& npcStats = getPlayer().getClass().getNpcStats(getPlayer());
        MWMechanics::DynamicStat<float> health = creatureStats.getDynamic(0);
        creatureStats.setHealth(int(health.getBase() * gmst.find("fWereWolfHealth")->mValue.getFloat()));
        for(size_t i = 0;i < ESM::Attribute::Length;++i)
        {
            // Oh, Bethesda. It's "Intelligence".
            std::string name = "fWerewolf"+((i==ESM::Attribute::Intelligence) ? std::string("Intellegence") :
                                            ESM::Attribute::sAttributeNames[i]);

            MWMechanics::AttributeValue value = npcStats.getAttribute(i);
            value.setBase(int(gmst.find(name)->mValue.getFloat()));
            npcStats.setAttribute(i, value);
        }

        for(size_t i = 0;i < ESM::Skill::Length;i++)
        {
            // Acrobatics is set separately for some reason.
            if(i == ESM::Skill::Acrobatics)
                continue;

            // "Mercantile"! >_<
            std::string name = "fWerewolf"+((i==ESM::Skill::Mercantile) ? std::string("Merchantile") :
                                            ESM::Skill::sSkillNames[i]);

            MWMechanics::SkillValue value = npcStats.getSkill(i);
            value.setBase(int(gmst.find(name)->mValue.getFloat()));
            npcStats.setSkill(i, value);
        }
    }

    void Player::set(const ESM::NPC *player)
    {
        mPlayer.mBase = player;
    }

    void Player::setCell (MWWorld::CellStore *cellStore)
    {
        mCellStore = cellStore;
    }

    MWWorld::Ptr Player::getPlayer()
    {
        MWWorld::Ptr ptr (&mPlayer, mCellStore);
        return ptr;
    }

    MWWorld::ConstPtr Player::getConstPlayer() const
    {
        MWWorld::ConstPtr ptr (&mPlayer, mCellStore);
        return ptr;
    }

    void Player::setBirthSign (const std::string &sign)
    {
        mSign = sign;
    }

    const std::string& Player::getBirthSign() const
    {
        return mSign;
    }

    void Player::setDrawState (MWMechanics::DrawState_ state)
    {
         MWWorld::Ptr ptr = getPlayer();
         ptr.getClass().getNpcStats(ptr).setDrawState (state);
    }

    bool Player::getAutoMove() const
    {
        return mAutoMove;
    }

    void Player::setAutoMove (bool enable)
    {
        MWWorld::Ptr ptr = getPlayer();

        mAutoMove = enable;
        float value = mForwardBackward;
        if (mAutoMove)
            value = 1.f;

        if (isMounted())
        {
            mMountForwardBackward = value;
            ptr.getClass().getMovementSettings(ptr).mPosition[1] = 0.f;
        }
        else
            ptr.getClass().getMovementSettings(ptr).mPosition[1] = value;
    }

    void Player::setLeftRight (float value)
    {
        MWWorld::Ptr ptr = getPlayer();
        if (isMounted())
        {
            mMountLeftRight = value;
            ptr.getClass().getMovementSettings(ptr).mPosition[0] = 0.f;
        }
        else
            ptr.getClass().getMovementSettings(ptr).mPosition[0] = value;
    }

    void Player::setForwardBackward (float value)
    {
        MWWorld::Ptr ptr = getPlayer();

        mForwardBackward = value;
        if (mAutoMove)
            value = 1.f;

        if (isMounted())
        {
            mMountForwardBackward = value;
            ptr.getClass().getMovementSettings(ptr).mPosition[1] = 0.f;
        }
        else
            ptr.getClass().getMovementSettings(ptr).mPosition[1] = value;
    }

    void Player::setUpDown(int value)
    {
        MWWorld::Ptr ptr = getPlayer();
        // Jump/vertical movement is intentionally suppressed while mounted.
        ptr.getClass().getMovementSettings(ptr).mPosition[2] = isMounted() ? 0.f : static_cast<float>(value);
    }

    void Player::setRunState(bool run)
    {
        MWWorld::Ptr ptr = getPlayer();
        if (isMounted())
        {
            mMountRun = run;
            ptr.getClass().getCreatureStats(ptr).setMovementFlag(MWMechanics::CreatureStats::Flag_Run, false);
        }
        else
            ptr.getClass().getCreatureStats(ptr).setMovementFlag(MWMechanics::CreatureStats::Flag_Run, run);
    }

    void Player::setSneak(bool sneak)
    {
        MWWorld::Ptr ptr = getPlayer();
        if (isMounted())
        {
            ptr.getClass().getCreatureStats(ptr).setMovementFlag(MWMechanics::CreatureStats::Flag_Sneak, false);
            return;
        }

        // Sneak is the dismount button. Do not immediately crouch after a
        // dismount until the key/button has actually been released.
        if (mDismountSneakLatch)
        {
            if (!sneak)
                mDismountSneakLatch = false;
            ptr.getClass().getCreatureStats(ptr).setMovementFlag(MWMechanics::CreatureStats::Flag_Sneak, false);
            return;
        }
        ptr.getClass().getCreatureStats(ptr).setMovementFlag(MWMechanics::CreatureStats::Flag_Sneak, sneak);
    }

    void Player::yaw(float yaw)
    {
        MWWorld::Ptr ptr = getPlayer();
        if (isMounted())
        {
            mMountYawDelta += yaw;
            ptr.getClass().getMovementSettings(ptr).mRotation[2] = 0.f;
        }
        else
            ptr.getClass().getMovementSettings(ptr).mRotation[2] += yaw;
    }
    void Player::pitch(float pitch)
    {
        MWWorld::Ptr ptr = getPlayer();
        ptr.getClass().getMovementSettings(ptr).mRotation[0] += pitch;
    }
    void Player::roll(float roll)
    {
        MWWorld::Ptr ptr = getPlayer();
        ptr.getClass().getMovementSettings(ptr).mRotation[1] += roll;
    }

    MWMechanics::DrawState_ Player::getDrawState()
    {
         MWWorld::Ptr ptr = getPlayer();
         return ptr.getClass().getNpcStats(ptr).getDrawState();
    }

    void Player::activate()
    {
        if (MWBase::Environment::get().getWindowManager()->isGuiMode())
            return;

        MWWorld::Ptr player = getPlayer();
        const MWMechanics::NpcStats &playerStats = player.getClass().getNpcStats(player);
        bool godmode = MWBase::Environment::get().getWorld()->getGodModeState();
        if ((!godmode && playerStats.isParalyzed()) || playerStats.getKnockedDown() || playerStats.isDead())
            return;

        MWWorld::Ptr toActivate = MWBase::Environment::get().getWorld()->getFacedObject();

        if (toActivate.isEmpty())
            return;

        if (!toActivate.getClass().hasToolTip(toActivate))
            return;

        // Y003s: guar_pack is handled entirely by the native riding system.
        if (tryMount(toActivate))
            return;

        // Activating doors/containers while the rider is physically attached to a
        // mount can split the two actors across cells. Dismount first instead.
        if (isMounted())
            return;

        MWBase::Environment::get().getWorld()->activate(toActivate, player);
    }

    bool Player::isMounted() const
    {
        return !mMount.isEmpty();
    }

    MWWorld::Ptr Player::getMount() const
    {
        return mMount;
    }

    bool Player::tryMount(const MWWorld::Ptr& mount)
    {
        if (mount.isEmpty() || !mount.isInCell() || !mount.getClass().isActor()
            || !Misc::StringUtils::ciEqual(mount.getCellRef().getRefId(), "guar_pack"))
            return false;

        if (isMounted())
        {
            if (mMount == mount)
            {
                requestDismount();
                return true;
            }
            return false;
        }

        MWMechanics::CreatureStats& mountStats = mount.getClass().getCreatureStats(mount);
        if (mountStats.isDead() || mountStats.getHealth().getCurrent() <= 0.f)
            return false;

        MWWorld::Ptr player = getPlayer();
        mMount = mount;
        mMountLeftRight = 0.f;
        mMountForwardBackward = mAutoMove ? 1.f : mForwardBackward;
        mMountYawDelta = 0.f;
        mMountRun = player.getClass().getCreatureStats(player)
            .getMovementFlag(MWMechanics::CreatureStats::Flag_Run);
        mDismountSneakLatch = false;

        // Keep the authored AI sequence intact. Riding overrides locomotion and
        // steering after AI has updated, so Wander/Combat resumes naturally on
        // dismount instead of being permanently erased from the creature.
        mountStats.setMovementFlag(MWMechanics::CreatureStats::Flag_Sneak, false);
        MWMechanics::Movement& mountMovement = mount.getClass().getMovementSettings(mount);
        mountMovement.mPosition[0] = 0.f;
        mountMovement.mPosition[1] = 0.f;
        mountMovement.mPosition[2] = 0.f;

        MWMechanics::Movement& playerMovement = player.getClass().getMovementSettings(player);
        playerMovement.mPosition[0] = 0.f;
        playerMovement.mPosition[1] = 0.f;
        playerMovement.mPosition[2] = 0.f;
        player.getClass().getCreatureStats(player).setMovementFlag(MWMechanics::CreatureStats::Flag_Sneak, false);
        MWBase::Environment::get().getWorld()->setActorCollisionMode(player, false, true);
        updateRiding();
        return true;
    }

    void Player::requestDismount()
    {
        if (!isMounted())
            return;
        mDismountSneakLatch = true;
        dismount();
    }

    void Player::dismount()
    {
        if (!isMounted())
            return;

        MWWorld::Ptr mount = mMount;
        mMount = MWWorld::Ptr();
        mMountLeftRight = 0.f;
        mMountForwardBackward = 0.f;
        mMountYawDelta = 0.f;
        mMountRun = false;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = getPlayer();
        world->setActorCollisionMode(player, true, true);

        if (!mount.isEmpty() && mount.isInCell())
        {
            MWMechanics::Movement& mountMovement = mount.getClass().getMovementSettings(mount);
            mountMovement.mPosition[0] = 0.f;
            mountMovement.mPosition[1] = 0.f;
            mountMovement.mPosition[2] = 0.f;

            const ESM::Position& mountPosition = mount.getRefData().getPosition();
            const float angle = mountPosition.rot[2];
            const osg::Vec3f mountHalf = world->getHalfExtents(mount);
            const osg::Vec3f playerHalf = world->getHalfExtents(player);
            const float sideDistance = std::max(48.f, mountHalf.x() + playerHalf.x() + 24.f);
            const float x = mountPosition.pos[0] + std::cos(angle) * sideDistance;
            const float y = mountPosition.pos[1] - std::sin(angle) * sideDistance;
            const float z = mountPosition.pos[2];

            player = world->moveObject(player, mount.getCell(), x, y, z, true);
            world->adjustPosition(player, true);
        }

        MWMechanics::Movement& playerMovement = player.getClass().getMovementSettings(player);
        playerMovement.mPosition[0] = 0.f;
        playerMovement.mPosition[1] = 0.f;
        playerMovement.mPosition[2] = 0.f;
    }

    void Player::applyMountControls(float duration)
    {
        if (!isMounted())
            return;

        MWMechanics::CreatureStats& stats = mMount.getClass().getCreatureStats(mMount);
        if (stats.isDead() || stats.getHealth().getCurrent() <= 0.f)
        {
            dismount();
            return;
        }

        MWMechanics::Movement& movement = mMount.getClass().getMovementSettings(mMount);
        // Riding uses A/D as steering rather than creature strafing. Mouse/stick
        // yaw is accumulated separately, so both control styles can be mixed.
        movement.mPosition[0] = 0.f;
        movement.mPosition[1] = mAutoMove ? 1.f : mMountForwardBackward;
        movement.mPosition[2] = 0.f;
        const float turnRate = mMountRun ? 1.35f : 1.75f;
        movement.mRotation[0] = 0.f;
        movement.mRotation[1] = 0.f;
        movement.mRotation[2] = mMountYawDelta + mMountLeftRight * turnRate * std::max(0.f, duration);
        mMountYawDelta = 0.f;
        stats.setMovementFlag(MWMechanics::CreatureStats::Flag_Run, mMountRun);
        stats.setMovementFlag(MWMechanics::CreatureStats::Flag_Sneak, false);

        MWWorld::Ptr player = getPlayer();
        MWMechanics::Movement& playerMovement = player.getClass().getMovementSettings(player);
        playerMovement.mPosition[0] = 0.f;
        playerMovement.mPosition[1] = 0.f;
        playerMovement.mPosition[2] = 0.f;
    }

    void Player::updateRiding()
    {
        if (!isMounted())
            return;

        MWWorld::Ptr player = getPlayer();
        if (!mMount.isInCell() || mMount.getRefData().getCount() <= 0
            || !mMount.getRefData().isEnabled() || mMount.getRefData().isDeleted()
            || mMount.getClass().getCreatureStats(mMount).isDead()
            || mMount.getClass().getCreatureStats(mMount).getHealth().getCurrent() <= 0.f
            || player.getClass().getCreatureStats(player).isDead())
        {
            dismount();
            return;
        }

        MWBase::World* world = MWBase::Environment::get().getWorld();
        const ESM::Position& mountPosition = mMount.getRefData().getPosition();
        const osg::Vec3f mountHalf = world->getHalfExtents(mMount);

        // Keep the player origin on the guar's back. This first engine-only
        // implementation deliberately reuses the ordinary player model/animation;
        // a dedicated seated skeleton can be layered on later without changing the
        // movement/network model.
        const float seatZ = mountPosition.pos[2] + std::max(36.f, mountHalf.z() * 1.35f);
        player = world->moveObject(player, mMount.getCell(),
            mountPosition.pos[0], mountPosition.pos[1], seatZ, false);
        world->rotateObject(player, 0.f, 0.f, mountPosition.rot[2]);
    }

    bool Player::wasTeleported() const
    {
        return mTeleported;
    }

    void Player::setTeleported(bool teleported)
    {
        mTeleported = teleported;
    }

    void Player::setAttackingOrSpell(bool attackingOrSpell)
    {
        mAttackingOrSpell = attackingOrSpell;
    }

    bool Player::getAttackingOrSpell() const
    {
        return mAttackingOrSpell;
    }

    void Player::setJumping(bool jumping)
    {
        mJumping = jumping;
    }

    bool Player::getJumping() const
    {
        return mJumping;
    }

    bool Player::isInCombat() {
        return MWBase::Environment::get().getMechanicsManager()->getActorsFighting(getPlayer()).size() != 0;
    }

    bool Player::enemiesNearby()
    {
        return MWBase::Environment::get().getMechanicsManager()->getEnemiesNearby(getPlayer()).size() != 0;
    }

    void Player::markPosition(CellStore *markedCell, const ESM::Position& markedPosition)
    {
        mMarkedCell = markedCell;
        mMarkedPosition = markedPosition;
    }

    void Player::getMarkedPosition(CellStore*& markedCell, ESM::Position &markedPosition) const
    {
        markedCell = mMarkedCell;
        if (mMarkedCell)
            markedPosition = mMarkedPosition;
    }

    void Player::clear()
    {
        mCellStore = nullptr;
        mSign.clear();
        mMarkedCell = nullptr;
        mAutoMove = false;
        mForwardBackward = 0;
        mTeleported = false;
        mAttackingOrSpell = false;
        mJumping = false;
        mMount = MWWorld::Ptr();
        mMountLeftRight = 0.f;
        mMountForwardBackward = 0.f;
        mMountYawDelta = 0.f;
        mMountRun = false;
        mDismountSneakLatch = false;
        mCurrentCrimeId = -1;
        mPaidCrimeId = -1;
        mPreviousItems.clear();
        mLastKnownExteriorPosition = osg::Vec3f(0,0,0);

        for (int i=0; i<ESM::Skill::Length; ++i)
        {
            mSaveSkills[i].setBase(0);
            mSaveSkills[i].setModifier(0);
        }

        for (int i=0; i<ESM::Attribute::Length; ++i)
        {
            mSaveAttributes[i].setBase(0);
            mSaveAttributes[i].setModifier(0);
        }

        mMarkedPosition.pos[0] = 0;
        mMarkedPosition.pos[1] = 0;
        mMarkedPosition.pos[2] = 0;
        mMarkedPosition.rot[0] = 0;
        mMarkedPosition.rot[1] = 0;
        mMarkedPosition.rot[2] = 0;
    }

    void Player::write (ESM::ESMWriter& writer, Loading::Listener& progress) const
    {
        ESM::Player player;

        mPlayer.save (player.mObject);
        player.mCellId = mCellStore->getCell()->getCellId();

        player.mCurrentCrimeId = mCurrentCrimeId;
        player.mPaidCrimeId = mPaidCrimeId;

        player.mBirthsign = mSign;

        player.mLastKnownExteriorPosition[0] = mLastKnownExteriorPosition.x();
        player.mLastKnownExteriorPosition[1] = mLastKnownExteriorPosition.y();
        player.mLastKnownExteriorPosition[2] = mLastKnownExteriorPosition.z();

        if (mMarkedCell)
        {
            player.mHasMark = true;
            player.mMarkedPosition = mMarkedPosition;
            player.mMarkedCell = mMarkedCell->getCell()->getCellId();
        }
        else
            player.mHasMark = false;

        for (int i=0; i<ESM::Attribute::Length; ++i)
            mSaveAttributes[i].writeState(player.mSaveAttributes[i]);
        for (int i=0; i<ESM::Skill::Length; ++i)
            mSaveSkills[i].writeState(player.mSaveSkills[i]);

        player.mPreviousItems = mPreviousItems;

        writer.startRecord (ESM::REC_PLAY);
        player.save (writer);
        writer.endRecord (ESM::REC_PLAY);
    }

    bool Player::readRecord (ESM::ESMReader& reader, uint32_t type)
    {
        if (type==ESM::REC_PLAY)
        {
            ESM::Player player;
            player.load (reader);

            if (!mPlayer.checkState (player.mObject))
            {
                // this is the one object we can not silently drop.
                throw std::runtime_error ("invalid player state record (object state)");
            }

            if (!player.mObject.mEnabled)
            {
                Log(Debug::Warning) << "Warning: Savegame attempted to disable the player.";
                player.mObject.mEnabled = true;
            }

            mPlayer.load (player.mObject);

            for (int i=0; i<ESM::Attribute::Length; ++i)
                mSaveAttributes[i].readState(player.mSaveAttributes[i]);
            for (int i=0; i<ESM::Skill::Length; ++i)
                mSaveSkills[i].readState(player.mSaveSkills[i]);

            if (player.mObject.mNpcStats.mWerewolfDeprecatedData && player.mObject.mNpcStats.mIsWerewolf)
            {
                saveStats();
                setWerewolfStats();
            }

            getPlayer().getClass().getCreatureStats(getPlayer()).getAiSequence().clear();

            MWBase::World& world = *MWBase::Environment::get().getWorld();

            try
            {
                mCellStore = world.getCell (player.mCellId);
            }
            catch (...)
            {
                Log(Debug::Warning) << "Warning: Player cell '" << player.mCellId.mWorldspace << "' no longer exists";
                // Cell no longer exists. The loader will have to choose a default cell.
                mCellStore = nullptr;
            }

            if (!player.mBirthsign.empty())
            {
                const ESM::BirthSign* sign = world.getStore().get<ESM::BirthSign>().search (player.mBirthsign);
                if (!sign)
                    throw std::runtime_error ("invalid player state record (birthsign does not exist)");
            }

            mCurrentCrimeId = player.mCurrentCrimeId;
            mPaidCrimeId = player.mPaidCrimeId;

            mSign = player.mBirthsign;

            mLastKnownExteriorPosition.x() = player.mLastKnownExteriorPosition[0];
            mLastKnownExteriorPosition.y() = player.mLastKnownExteriorPosition[1];
            mLastKnownExteriorPosition.z() = player.mLastKnownExteriorPosition[2];

            if (player.mHasMark && !player.mMarkedCell.mPaged)
            {
                // interior cell -> need to check if it exists (exterior cell will be
                // generated on the fly)

                if (!world.getStore().get<ESM::Cell>().search (player.mMarkedCell.mWorldspace))
                    player.mHasMark = false; // drop mark silently
            }

            if (player.mHasMark)
            {
                mMarkedPosition = player.mMarkedPosition;
                mMarkedCell = world.getCell (player.mMarkedCell);
            }
            else
            {
                mMarkedCell = nullptr;
            }

            mForwardBackward = 0;
            mTeleported = false;

            mPreviousItems = player.mPreviousItems;

            return true;
        }

        return false;
    }

    int Player::getNewCrimeId()
    {
        return ++mCurrentCrimeId;
    }

    void Player::recordCrimeId()
    {
        mPaidCrimeId = mCurrentCrimeId;
    }

    int Player::getCrimeId() const
    {
        return mPaidCrimeId;
    }

    void Player::setPreviousItem(const std::string& boundItemId, const std::string& previousItemId)
    {
        mPreviousItems[boundItemId] = previousItemId;
    }

    std::string Player::getPreviousItem(const std::string& boundItemId)
    {
        return mPreviousItems[boundItemId];
    }

    void Player::erasePreviousItem(const std::string& boundItemId)
    {
        mPreviousItems.erase(boundItemId);
    }

    void Player::setSelectedSpell(const std::string& spellId)
    {
        Ptr player = getPlayer();
        InventoryStore& store = player.getClass().getInventoryStore(player);
        store.setSelectedEnchantItem(store.end());
        int castChance = int(MWMechanics::getSpellSuccessChance(spellId, player));
        MWBase::Environment::get().getWindowManager()->setSelectedSpell(spellId, castChance);
        MWBase::Environment::get().getWindowManager()->updateSpellWindow();
    }
}
