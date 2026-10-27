#ifndef WOW_SOURCE_OBJECT_MOVEMENT_H
#define WOW_SOURCE_OBJECT_MOVEMENT_H

#include <cstddef>
#include <cstdint>
#include "WoW/Source/Object/WGUID.h"
#include "Engine/Source/Tempest/C2Vector.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/Storm/H/stpl.h"
#include "WoW/Source/Object/CPlayerMoveEvent.h"
#include "WoW/Source/Object/MovementShared.h"
#include "WoW/Source/Object/Types.h"

class CAaBox;
class C4Plane;
class CGUnit_C;
class CClientMoveUpdate;
class CDataStore;
class CClientObjCreate;
namespace World { class FacetData; }

struct ClipPolygon {
    /* 0000 */ C3Vector m_v [15];
    /* 00B4 */ int32_t m_flags [15];
    /* 00F0 */ int32_t m_count;

    ClipPolygon() = default;
    ClipPolygon(const ClipPolygon& source);
};

struct MoveState {
    /* 0000 */ C3Vector m_anchorPos;
    /* 000C */ float m_anchorFacing;
    /* 0010 */ float m_anchorPitch;
    /* 0014 */ uint32_t m_anchorElapsedMs;
    /* 0018 */ int32_t m_fallTimeMs;
    /* 001C */ C3Vector m_moveDir;
    /* 0028 */ C2Vector m_moveDir2D;
    /* 0030 */ uint32_t m_flags;
    /* 0034 */ float m_currentSpeed;
};

class CPlayerMoveQueue : public TSList<CPlayerMoveEvent, TSGetExplicitLink<CPlayerMoveEvent> > {
    public:
    static TSList<CPlayerMoveEvent, TSGetExplicitLink<CPlayerMoveEvent> > s_freeList;

    static CPlayerMoveEvent* AllocEvent(uint32_t eventTime, MoveEventId eventId);

    void Enqueue(CPlayerMoveEvent* event);
    void FreeAll();
    void FreeAllExceptTimeSync();
    int32_t HasEventBetween(MoveEventId first, MoveEventId last);
    int32_t RequeueEntry(MoveEventId eventId, uint32_t eventTime);
    void UnlinkEventById(MoveEventId eventId);
};

class CMovement_C : public CMovementShared {
    public:
    static void MoveUnits(uint32_t time, uint32_t lastUpdateTime);
    // WarcraftXL: empties the facet query the moves reuse, so that the next one collects the
    // ground's facets again (a mod reshaped the ground).
    static void ForgetFacetQuery();
    static void WrapFacingToRange(float& facing, float min, float max);

    /* 00C4 */ uint32_t m_gameTime;
    /* 00C8 */ float m_collisionRadius;
    /* 00CC */ float m_collisionHeight;
    /* 00D0 */ float m_stepUpHeight;
    /* 00D4 */ C3Vector m_interpolationPos;
    /* 00E0 */ float m_interpolationFacing;
    /* 00E4 */ float m_interpolationPitch;
    /* 00E8 */ int16_t m_timeSkewSamples [32];
    /* 0128 */ uint32_t m_timeSkewIndex;
    /* 012C */ uint32_t m_timeSkew;
    /* 0130 */ uint32_t m_interpolation;
    /* 0134 */ uint32_t m_moveStartDelay;
    /* 0138 */ CPlayerMoveQueue m_moveQueue;
    /* 0144 */ CGUnit_C* m_unit;

    CMovement_C(const C3Vector& position, float facing, const WOWGUID& ownerGuid, CGUnit_C* unit);
    ~CMovement_C();
    void SetUpdateInfo(uint32_t time, const CClientMoveUpdate* update, int32_t activePlayer);
    void AddPlayerChangeSeatsEvent(uint32_t eventTime, MoveEventId eventId, bool needAck, WOWGUID transport, uint8_t seat);
    void AddPlayerMoveEvent(uint32_t eventTime, MoveEventId eventId, bool needAck, uint32_t ackCounter, float facing, float pitch, uint16_t extraFlags);
    int32_t AddTimeSkewSample(uint32_t localTime, uint32_t serverTime);
    void BuildCollisionBox(const C3Vector& position, CAaBox& box) const;
    void BuildCollisionVolume(const C3Vector& position, float height, C4Plane* planes, C3Vector* points, uint8_t* faces) const;
    void BuildCollisionVolumePlanes(const C3Vector& position, float height, float radius, C4Plane* planes) const;
    C3Vector CalcRunWalkBlockingObstaclePush(const C3Vector& move, float dt, float maxDistance, const C3Vector& normal) const;
    C3Vector CalcRunWalkWalkableObstaclePush(const C3Vector& move, float* scale, C3Vector normal, int32_t useAltNormal, const C3Vector& altNormal) const;
    int32_t CheckFallImpactThreshold(float timeFallen, float elapsed, const C3Vector& move, int32_t force) const;
    int32_t ComputeSteepSurfacePushNormalImpl(const C4Plane& plane) const;
    void FixupQueuedSetFacingEvents(float delta);
    uint32_t GetMoveStartTime(uint32_t elapsed);
    void GetMoveStatus(NETMESSAGE msgId, uint32_t gameTime, CMovementStatus* status) const;
    float GetTimeJustFallen(float elapsed, float timeFallen, C3Vector* move, float* distance, int32_t force, float speed);
    uint32_t GetFacetQueryFlags() const;
    float GetRemainingStepUpHeight() const;
    float GetStepUpHeight() const;
    int32_t HandlePendingActions();
    int32_t Interpolate(uint32_t now, uint32_t elapsed, C3Vector* position, float* facing, float* pitch);
    int32_t IsFacetOverhead(uint32_t facet) const;
    int32_t IsFacetWall(uint32_t facet) const;
    int32_t IsJumping() const;
    int32_t IsValidPosition() const;
    void Launch(const C2Vector& direction, float speed, float verticalSpeed);
    void OnAscendDescendStartLocal(uint32_t eventTime, int32_t ascend);
    void OnAscendDescendStopLocal(uint32_t eventTime);
    void OnCanFlyLocal(uint32_t eventTime, uint32_t ackCounter, int32_t canFly);
    void OnCanTransitionBetweenSwimAndFlyLocal(uint32_t eventTime, uint32_t ackCounter, int32_t canTransition);
    void OnChangeSeatsOnControlledVehicleLocal(uint32_t eventTime, WOWGUID transport, uint8_t seat);
    void OnDisableGravityLocal(uint32_t eventTime, uint32_t ackCounter, int32_t disable);
    void OnDismissControlledVehicleLocal(uint32_t eventTime);
    void OnFallLocal(uint32_t eventTime);
    int32_t TryStartFallingLocal();
    void OnFeatherFall(int32_t enable);
    void OnFeatherFallLocal(uint32_t eventTime, uint32_t ackCounter, int32_t featherFall);
    int32_t OnFlightBackSpeedChange(float value);
    void OnFlightBackSpeedChange(uint32_t eventTime, float value);
    void OnFlightBackSpeedChangeLocal(uint32_t eventTime, uint32_t ackCounter, float value);
    void OnFlightLocal(uint32_t eventTime, int32_t flying);
    int32_t OnFlightSpeedChange(float value);
    void OnFlightSpeedChange(uint32_t eventTime, float value);
    void OnFlightSpeedChangeLocal(uint32_t eventTime, uint32_t ackCounter, float value);
    void OnHoverLocal(uint32_t eventTime, uint32_t ackCounter, int32_t hover);
    void OnJumpLocal(uint32_t eventTime);
    void OnLaunchLocal(uint32_t eventTime, uint32_t ackCounter, const C2Vector& direction, float speed, float verticalSpeed);
    void OnMoveStartLocal(uint32_t eventTime, int32_t forward);
    void OnMoveStopLocal(uint32_t eventTime);
    int32_t OnPitchRateChange(float value);
    void OnPitchRateChange(uint32_t eventTime, float value);
    void OnPitchRateChangeLocal(uint32_t eventTime, uint32_t ackCounter, float value);
    void OnPitchStopLocal(uint32_t eventTime);
    void OnPitchToAngleLocal(uint32_t eventTime, float pitch);
    void OnTurnToAngleLocal(uint32_t eventTime, float facing);
    int32_t ComputeLegalRawFacingRange(float& min, float& max) const;
    void OnRootLocal(uint32_t eventTime, uint32_t ackCounter, int32_t root);
    int32_t OnRunBackSpeedChange(float value);
    void OnRunBackSpeedChange(uint32_t eventTime, float value);
    void OnRunBackSpeedChangeLocal(uint32_t eventTime, uint32_t ackCounter, float value);
    int32_t OnRunSpeedChange(float value);
    void OnRunSpeedChange(uint32_t eventTime, float value);
    void OnRunSpeedChangeLocal(uint32_t eventTime, uint32_t ackCounter, float value);
    void OnSetCollisionHeight(uint32_t eventTime, float value);
    void OnSetCollisionHeightLocal(uint32_t eventTime, uint32_t ackCounter, float value);
    void OnSetPitchLocal(uint32_t eventTime, float pitch);
    void OnSetRawFacingLocal(uint32_t eventTime, float facing);
    void OnSetRunMode(int32_t run);
    void OnSetRunModeLocal(uint32_t eventTime, int32_t run);
    void OnSkipTime(uint32_t time);
    void OnStrafeStartLocal(uint32_t eventTime, int32_t left);
    void OnStrafeStopLocal(uint32_t eventTime);
    int32_t OnSwimBackSpeedChange(float value);
    void OnSwimBackSpeedChange(uint32_t eventTime, float value);
    void OnSwimBackSpeedChangeLocal(uint32_t eventTime, uint32_t ackCounter, float value);
    int32_t OnSwimSpeedChange(float value);
    void OnSwimSpeedChange(uint32_t eventTime, float value);
    void OnSwimSpeedChangeLocal(uint32_t eventTime, uint32_t ackCounter, float value);
    void OnSwimStart(uint32_t eventTime);
    void OnSwimStop(uint32_t eventTime);
    void OnTimeSyncRequest(uint32_t eventTime, uint32_t counter);
    int32_t OnTurnRateChange(float value);
    void OnTurnRateChange(uint32_t eventTime, float value);
    void OnTurnRateChangeLocal(uint32_t eventTime, uint32_t ackCounter, float value);
    void OnTurnStopLocal(uint32_t eventTime);
    void OnTurnStartLocal(uint32_t eventTime, int32_t left);
    void OnPitchStartLocal(uint32_t eventTime, int32_t up);
    void OnSwimStartLocal(uint32_t eventTime);
    void OnSwimStopLocal(uint32_t eventTime);
    int32_t OnWalkSpeedChange(float value);
    void OnWalkSpeedChange(uint32_t eventTime, float value);
    void OnWalkSpeedChangeLocal(uint32_t eventTime, uint32_t ackCounter, float value);
    void OnWaterWalk(int32_t enable);
    void OnWaterWalkLocal(uint32_t eventTime, uint32_t ackCounter, int32_t waterWalk);
    int32_t PlaneIntersectsVolume(const C4Plane& plane) const;
    C3Vector PushOffObstacleEdge(const C3Vector& point, const C3Vector& direction, uint32_t facet) const;
    void QueueFallEvent(uint32_t eventTime);
    void RestoreMoveState(const MoveState& state);
    void SaveMoveState(MoveState& state) const;
    void SendTimeSyncResp(uint32_t clientTime, uint32_t counter);
    void SetCollisionBox(float width, float height, float scale, float referenceScale, int32_t force);
    int32_t CollisionBoxDoesntFit(float width, float height, float scale);
    int32_t CollisionBoxDoesntFit(const CAaBox& box);
    int32_t BoxDoesntFit(float height, float radius);
    void SetInterpolation(uint32_t time);
    void SetSplineFaceAngle(float facing);
    void SetSplineFacePoint(const C3Vector& point);
    void SetSplineFaceTarget(const WOWGUID& target);
    void SplineToggleEnableFlying(int32_t enable);
    void SnapToSpline(const C3Vector& position, int32_t force);
    void UpdateHeartbeatTimerA(uint32_t time);
    void WriteMovementStatusToPacket(uint32_t msgId, uint32_t time, CDataStore* msg) const;
    void ValidateWorldPosition(const C3Vector& position) const;
    int32_t UpdateTransportStatus(const CClientObjCreate& create);
    int32_t UpdateTransportStatus(WOWGUID transport, uint8_t seat);
    void FlushMoveQueue(int32_t a1, int32_t a2);
    void RemoveFromMoversList(int32_t a1);
    int32_t UpdateStatusInternal(uint32_t time, const CMovementStatus& status, int32_t& timeOffset, int32_t local, int32_t force);
    void AddPlayerSpeedEvent(uint32_t eventTime, MoveEventId eventId, bool needAck, uint32_t ackCounter, float value, const CMovementStatus* status);
    void AddPlayerMoveEvent(uint32_t eventTime, MoveEventId eventId, bool needAck, uint32_t ackCounter, const C2Vector& direction, float speed, float verticalSpeed, const CMovementStatus* status);
    void AddPlayerTeleportEvent(uint32_t eventTime, bool needAck, uint32_t ackCounter, const CMovementStatus& status);
    void OnTeleportLocal(uint32_t eventTime, uint32_t ackCounter, const CMovementStatus& status);
    int32_t UpdateLaunchStatus(uint32_t time, const CMovementStatus& status, MoveEventId eventId, const C2Vector& direction, float speed, float verticalSpeed);
    void OnSetActiveMover();
    void OnUnsetActiveMover();
    void FlushMoveEvent(CPlayerMoveEvent* event, int32_t a3);
    void Extrapolate(uint32_t time, uint32_t elapsed);
    int32_t UpdateStatus(uint32_t time, const CMovementStatus& status, MoveEventId eventId, float value);
    int32_t UpdateFacingStatus(uint32_t time, const CMovementStatus& status);
    int32_t OnMoveStart(uint32_t time, const CMovementStatus& status, int32_t forward);
    int32_t OnStrafeStart(uint32_t time, const CMovementStatus& status, int32_t left);
    int32_t OnJump(uint32_t time, const CMovementStatus& status);
    int32_t OnMoveStop(uint32_t time, const CMovementStatus& status);
    int32_t OnStrafeStop(uint32_t time, const CMovementStatus& status);
    int32_t OnTurnStart(uint32_t time, const CMovementStatus& status, int32_t left);
    int32_t OnTurnStop(uint32_t time, const CMovementStatus& status);
    int32_t OnPitchStart(uint32_t time, const CMovementStatus& status, int32_t up);
    int32_t OnPitchStop(uint32_t time, const CMovementStatus& status);
    int32_t OnSetRunMode(uint32_t time, const CMovementStatus& status, int32_t run);
    int32_t OnHeartBeat(uint32_t time, const CMovementStatus& status);
    int32_t OnSetFacing(uint32_t time, const CMovementStatus& status);
    int32_t OnSetPitch(uint32_t time, const CMovementStatus& status);
    int32_t OnMoveRoot(uint32_t time, const CMovementStatus& status);
    int32_t OnMoveUnRoot(uint32_t time, const CMovementStatus& status);
    int32_t OnGravityChange(uint32_t time, const CMovementStatus& status);
    int32_t OnStartSwim(uint32_t time, const CMovementStatus& status);
    int32_t OnStopSwim(uint32_t time, const CMovementStatus& status);
    int32_t OnSetWaterWalk(uint32_t time, const CMovementStatus& status);
    int32_t OnSetFeatherFall(uint32_t time, const CMovementStatus& status);
    int32_t OnMoveHover(uint32_t time, const CMovementStatus& status);
    int32_t OnUpdateCanFly(uint32_t time, const CMovementStatus& status);
    int32_t OnUpdateCanTransitionBetweenSwimAndFly(uint32_t time, const CMovementStatus& status);
    int32_t OnStartAscendDescend(uint32_t time, const CMovementStatus& status, int32_t ascend);
    int32_t OnStopAscend(uint32_t time, const CMovementStatus& status);
    int32_t OnTeleport(uint32_t time, const CMovementStatus& status);
    int32_t OnLaunch(uint32_t time, const CMovementStatus& status, const C2Vector& direction, float speed, float verticalSpeed);
    int32_t OnRunSpeedChangeOther(uint32_t time, const CMovementStatus& status, float value);
    int32_t OnRunBackSpeedChangeOther(uint32_t time, const CMovementStatus& status, float value);
    int32_t OnWalkSpeedChangeOther(uint32_t time, const CMovementStatus& status, float value);
    int32_t OnSwimSpeedChangeOther(uint32_t time, const CMovementStatus& status, float value);
    int32_t OnSwimBackSpeedChangeOther(uint32_t time, const CMovementStatus& status, float value);
    int32_t OnFlightSpeedChangeOther(uint32_t time, const CMovementStatus& status, float value);
    int32_t OnFlightBackSpeedChangeOther(uint32_t time, const CMovementStatus& status, float value);
    int32_t OnTurnRateChangeOther(uint32_t time, const CMovementStatus& status, float value);
    int32_t OnPitchRateChangeOther(uint32_t time, const CMovementStatus& status, float value);
    int32_t OnSetCollisionHeightOther(uint32_t time, const CMovementStatus& status, float value);
    void OnSplineStartSwim();
    int32_t OnSpline(const C3Vector* points, uint32_t pointCount, uint32_t duration, uint32_t splineFlags, uint32_t splineId);
    void OnSplineSnap(uint32_t splineId, const C3Vector& destination, uint32_t splineFlags, int32_t flush);
    void SetFacingAndFlush(float facing, int32_t flush);
    void ForceSetTransport(WOWGUID transport, uint8_t seat, int32_t a3);
    void SetSplineParabolic(float verticalAcceleration, uint32_t effectStartTime);
    void SetSplineAnimation(uint8_t animType, uint32_t effectStartTime);
    void OnSplineStopSwim();
    void OnSplineToggleHover(int32_t hover);
    void OnSplineRoot();
    void OnSplineUnRoot();
    int32_t OnSplineEnableGravity(int32_t enable);
    void UpdateHoverState(int32_t hover, int32_t a3);
    void SnapToGroundIfCloseEnough();
    void Disable();
    void SendFallReset(uint32_t time);
    void GroundNormal();
    uint32_t UseWalkableRedirection(uint32_t facet, int32_t* cleared);
    void MoveSplineMoverWithoutCollision(const C3Vector& position, uint32_t time, uint32_t elapsed);
    int32_t ValidateTestVsFacetQuery(C3Vector move) const;
    int32_t GetCurrentHoverHeight(float* height, int32_t* noGround, uint64_t* facetId) const;
    void CollideWithTransport(uint32_t time, uint32_t elapsed, float distance, const C3Vector& direction, const C44Matrix& transportMatrix);
    int32_t DistanceToMove(World::FacetData* facets, const C3Vector& direction, float distance, uint32_t* facetIndex, C4Plane* planes, uint32_t* planeCount, float* moveDistance, const C3Vector* position) const;
    void CallMoveEventHandlers(uint32_t time, uint32_t remaining, uint32_t oldFlags, uint32_t oldFlags2, int32_t jumping, int32_t transportChanged);
    int32_t GetMoveFacets(float distance, uint32_t elapsed, C3Vector direction);
    int32_t SetTransport(WOWGUID transport);
    int32_t ForceSetTransportInt(WOWGUID transport, uint8_t seat, int32_t a3);
    uint32_t Swim(uint32_t time, uint32_t elapsed, float distance, C3Vector direction);
    int32_t TryFallingDown(float timeFallen, C3Vector* move, float* distance, C2Vector* push, uint64_t* facetId, int32_t* onSlope, int32_t* landed);
    uint32_t FallDown(uint32_t time, uint32_t elapsed, C3Vector move, int32_t notify);
    uint32_t Fall(uint32_t time, uint32_t elapsed, float distance, const C2Vector& direction);
    int32_t WillPassObstacle(const C2Vector& direction, const C3Vector& position, float height);
    int32_t AttemptStepUp(const C2Vector& direction, C3Vector normal);
    uint32_t TraceSurface(uint32_t time, uint32_t elapsed, float distance, const C2Vector& direction);
    uint32_t HoverMove(uint32_t time, uint32_t elapsed, float distance, C3Vector direction);
    uint32_t CollideRequestMove(uint32_t time, uint32_t elapsed, C3Vector move);
    void OnSplineStop(uint32_t time);
    void SkipTime(uint32_t time);
    int32_t UpdatePlayerMovement(uint32_t time);
    int32_t UpdatePitch(uint32_t time, CPlayerMoveEvent* event);
    void SetCollisionHeight(float height);
    void Halt();
    void FinishSpline(int32_t force);
    int32_t HeartBeat(const CPlayerMoveEvent* event);
    void GravityStateChanged();
    int32_t FallStateChangedLocal(uint32_t time, uint32_t msgId, uint32_t oldFlags, uint32_t oldFlags2, bool needAck, float value);
    int32_t TryStartFlight();
    int32_t InsertTurnToAngleStop(uint32_t time, float facing);
    int32_t InsertPitchToAngleStop(uint32_t time, float pitch);
    void Teleport(WOWGUID transport, const C3Vector& position, float facing, int32_t a4, int32_t needAck, uint8_t seat);
    uint32_t RequestMove(uint32_t time, uint32_t elapsed, const C3Vector& move);
    int32_t PlotUnitSplineMovement(uint32_t time, uint32_t elapsed, C3Vector* position);
    void ApplyMovement(uint32_t time, uint32_t elapsed);
    void ExecuteMovement(uint32_t time, uint32_t lastTime);
    int32_t IsSlopeFallable(uint32_t facet, const C3Vector& move) const;
    C3Vector GetSteepSurfacePushNormal(uint32_t facet, const C4Plane* planes, uint32_t count);
    C2Vector CalcFallObstaclePush(const C3Vector& direction, float distance, float maxDistance, uint32_t facet, const C4Plane* planes, uint32_t count);
};

#endif
