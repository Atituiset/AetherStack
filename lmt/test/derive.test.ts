// Node-run unit assertions for the multi-call / QoS derivations in
// src/services.ts. No test runner in this project: compiled by tsc to
// CommonJS and executed with plain node (see run.sh).
declare const process: { exit(code: number): void } // no @types/node in this repo
import { LogEvent } from '../src/hooks/useWebSocket'
import {
  svc, deriveCalls, deriveBearers, deriveConfs, deriveRadio, deriveSleep, deriveAuth, deriveMobility, deriveChat,
  bearerClassOf, confUeIds, sinrBars, qualityTier, mcsLabel, cellOf, AUTH_CAUSE,
  CallMap, UeId,
} from '../src/services'
import { nodeOf } from '../src/nodes'

let seq = 0
const ev = (node: string, event: string, fields: Record<string, string> = {}): LogEvent => ({
  timestamp: new Date(Date.now() + seq * 10).toISOString(),
  module: node === 'bs' ? 'BS' : 'UE',
  node,
  level: 'INFO',
  event,
  fields,
  _seq: ++seq,
})

const IMSI = { ue1: '460011234567890', ue2: '460011234567891', ue3: '460011234567892' }
const CRNTI: Record<UeId, string> = { ue1: '4601', ue2: '4602', ue3: '4603' }

let passed = 0
let failed = 0
function check(name: string, cond: boolean, detail = '') {
  if (cond) {
    passed++
    console.log(`  ok    ${name}`)
  } else {
    failed++
    console.error(`  FAIL  ${name}${detail ? ` — ${detail}` : ''}`)
  }
}

/** Concurrent video (ue1<->ue2) + voice (ue1<->ue3) script, fully established. */
function concurrentScript(): LogEvent[] {
  return [
    ev('ue1', svc.SIP_INVITE_TX, { dst: IMSI.ue2, kind: 'video' }),
    ev('ue2', svc.SIP_INVITE_RX, { src: IMSI.ue1, kind: 'video' }),
    ev('ue1', svc.SIP_INVITE_TX, { dst: IMSI.ue3, kind: 'voice' }),
    ev('ue3', svc.SIP_INVITE_RX, { src: IMSI.ue1, kind: 'voice' }),
    ev('ue2', svc.SIP_CALL_ESTABLISHED, { peer: IMSI.ue1, kind: 'video' }),
    ev('ue1', svc.SIP_CALL_ESTABLISHED, { peer: IMSI.ue2, kind: 'video' }),
    ev('ue3', svc.SIP_CALL_ESTABLISHED, { peer: IMSI.ue1, kind: 'voice' }),
    ev('ue1', svc.SIP_CALL_ESTABLISHED, { peer: IMSI.ue3, kind: 'voice' }),
    ev('ue1', svc.APP_STREAM_STATS, { kind: 'video', peer: IMSI.ue2, qci: '2', tx: '100', rx: '90', loss: '1', rtt_avg: '24' }),
    ev('ue1', svc.APP_STREAM_STATS, { kind: 'voice', peer: IMSI.ue3, qci: '1', tx: '40', rx: '38', loss: '0', rtt_avg: '16' }),
    ev('ue2', svc.APP_STREAM_STATS, { kind: 'video', peer: IMSI.ue1, qci: '2', tx: '90', rx: '100', loss: '1', rtt_avg: '25' }),
    ev('ue3', svc.APP_STREAM_STATS, { kind: 'voice', peer: IMSI.ue1, qci: '1', tx: '38', rx: '40', loss: '0', rtt_avg: '15' }),
  ]
}

console.log('deriveCalls — concurrent calls keyed by (ue, kind)')
{
  const calls = deriveCalls(concurrentScript())
  const v = calls.ue1.video
  const w = calls.ue1.voice
  check('ue1 video call open + established', !!v && v.established)
  check('ue1 voice call open + established', !!w && w.established)
  check('ue1 video stats separate (tx=100, qci=2)', v?.stats?.tx === '100' && v?.stats?.qci === '2')
  check('ue1 voice stats separate (tx=40, qci=1)', w?.stats?.tx === '40' && w?.stats?.qci === '1')
  check('ue1 peers differ', v?.peer === IMSI.ue2 && w?.peer === IMSI.ue3)
  check('ue2 has video only', !!calls.ue2.video && !calls.ue2.voice)
  check('ue3 has voice only', !!calls.ue3.voice && !calls.ue3.video)
}

console.log('deriveCalls — BYE matches by peer, other dialog survives')
{
  const msgs = [...concurrentScript(), ev('ue1', svc.SIP_BYE_TX, { peer: IMSI.ue2 })]
  const calls = deriveCalls(msgs)
  check('ue1 video closed by BYE(peer=imsi2)', !calls.ue1.video)
  check('ue1 voice survives', !!calls.ue1.voice && calls.ue1.voice!.established)
}
{
  // legacy single-call fallback: BYE closes the only open call even without peer match
  const msgs = [
    ev('ue1', svc.SIP_INVITE_TX, { dst: IMSI.ue2, kind: 'voice' }),
    ev('ue1', svc.SIP_BYE_TX, {}),
  ]
  check('single open call closed by peer-less BYE', !deriveCalls(msgs).ue1.voice)
}

console.log('deriveCalls — APP_CALL_END / failure are attempt-scoped')
{
  const msgs = [
    ev('ue1', svc.SIP_INVITE_TX, { dst: IMSI.ue2, kind: 'voice' }),
    ev('ue1', svc.APP_CALL_END, { dst: IMSI.ue2, kind: 'video' }), // wrong kind: no-op on voice
  ]
  check('END of other kind leaves voice call open', !!deriveCalls(msgs).ue1.voice)
}
{
  const msgs = [
    // ue1 in an established video call with ue2 …
    ev('ue1', svc.SIP_INVITE_TX, { dst: IMSI.ue2, kind: 'video' }),
    ev('ue2', svc.SIP_INVITE_RX, { src: IMSI.ue1, kind: 'video' }),
    ev('ue2', svc.SIP_CALL_ESTABLISHED, { peer: IMSI.ue1, kind: 'video' }),
    ev('ue1', svc.SIP_CALL_ESTABLISHED, { peer: IMSI.ue2, kind: 'video' }),
    ev('ue1', svc.APP_STREAM_STATS, { kind: 'video', peer: IMSI.ue2, qci: '2', tx: '9', rx: '9', loss: '0', rtt_avg: '21' }),
    // … attempts a voice call to ue3 on top, and gets 486 (no kind on the wire)
    ev('ue1', svc.SIP_INVITE_TX, { dst: IMSI.ue3, kind: 'voice' }),
    ev('ue1', svc.SIP_CALL_FAILED, { peer: IMSI.ue3, reason: 'busy' }),
  ]
  const calls = deriveCalls(msgs)
  check('failed kind-less attempt closed (voice gone)', !calls.ue1.voice)
  check('established video survives SIP_CALL_FAILED', !!calls.ue1.video && calls.ue1.video!.established)
}

console.log('deriveCalls — window tolerance (stats-only revival, ended kinds stay dead)')
{
  const revived = deriveCalls([
    ev('ue2', svc.APP_STREAM_STATS, { kind: 'video', peer: IMSI.ue1, tx: '5', rx: '6', loss: '0', rtt_avg: '20' }),
  ])
  check('stats without START revive the call', !!revived.ue2.video && revived.ue2.video!.established)
  const ended = deriveCalls([
    ev('ue2', svc.APP_CALL_END, { dst: IMSI.ue1, kind: 'video' }),
    ev('ue2', svc.APP_STREAM_STATS, { kind: 'video', peer: IMSI.ue1, tx: '5', rx: '6', loss: '0', rtt_avg: '20' }),
    ev('ue2', svc.APP_STREAM_STATS, { kind: 'voice', peer: IMSI.ue1, tx: '1', rx: '1', loss: '0', rtt_avg: '9' }),
  ])
  check('ended kind is not revived by later stats', !ended.ue2.video)
  check('other kind still revives independently', !!ended.ue2.voice)
}

console.log('deriveBearers — setup/teardown, BS c_rnti mapping, tolerance')
{
  check('bearerClassOf qci mapping', bearerClassOf({ qci: '5' }) === 'sig' && bearerClassOf({ qci: '1' }) === 'voice'
    && bearerClassOf({ qci: '2' }) === 'video' && bearerClassOf({ qci: '9' }) === 'be')
  check('bearerClassOf kind fallback', bearerClassOf({ kind: 'video' }) === 'video' && bearerClassOf({}) === null)

  const b = deriveBearers([
    ev('ue1', svc.QOS_BEARER_SETUP, { c_rnti: CRNTI.ue1, qci: '5', kind: 'sig' }),
    ev('ue1', svc.QOS_BEARER_SETUP, { c_rnti: CRNTI.ue1, qci: '2', kind: 'video' }),
    ev('bs', svc.QOS_BEARER_SETUP, { c_rnti: CRNTI.ue3, qci: '1', kind: 'voice' }), // BS-side, mapped
    ev('bs', svc.QOS_BEARER_SETUP, { c_rnti: '9999', qci: '2', kind: 'video' }), // unknown: dropped
    ev('ue2', svc.QOS_BEARER_TEARDOWN, { c_rnti: CRNTI.ue2, qci: '1', kind: 'voice' }), // no setup: no-op
  ], CRNTI)
  check('ue1 sig+video pips in priority order', JSON.stringify(b.ue1) === '["sig","video"]', JSON.stringify(b.ue1))
  check('BS-side setup mapped via c_rnti to ue3', JSON.stringify(b.ue3) === '["voice"]', JSON.stringify(b.ue3))
  check('unknown c_rnti dropped, lone teardown is a no-op', b.ue2.length === 0)

  const torn = deriveBearers([
    ev('ue1', svc.QOS_BEARER_SETUP, { c_rnti: CRNTI.ue1, qci: '2', kind: 'video' }),
    ev('ue1', svc.QOS_BEARER_TEARDOWN, { c_rnti: CRNTI.ue1, qci: '2', kind: 'video' }),
  ], CRNTI)
  check('teardown removes the pip', torn.ue1.length === 0)
}

console.log('deriveChat — TX/RX dedup unchanged')
{
  const imsiMap: Record<UeId, string | null> = { ...IMSI }
  const chat = deriveChat([
    ev('ue1', svc.APP_MSG_TX, { dst: IMSI.ue2, text: 'hi' }),
    ev('ue2', svc.APP_MSG_RX, { src: IMSI.ue1, text: 'hi' }), // duplicate of the TX
    ev('ue3', svc.APP_MSG_RX, { src: IMSI.ue2, text: 'rx-only' }), // backend emits RX only
  ], imsiMap)
  check('forwarded message appears exactly once', chat.filter((c) => c.text === 'hi').length === 1)
  check('RX-only message still captured', chat.some((c) => c.text === 'rx-only' && c.from === 'ue2' && c.to === 'ue3'))
}

// silence unused-import warning when only some CallMap helpers are exercised
void ((c: CallMap | null) => c)(null)

console.log('deriveConfs — lifecycle, member-set tracking')
{
  const CID = '9001'
  const mid = deriveConfs([
    ev('ue1', svc.CONF_START, { host: IMSI.ue1, conf_id: CID }),
    ev('ue2', svc.CONF_JOIN, { conf_id: CID, imsi: IMSI.ue2 }),
    ev('ue3', svc.CONF_JOIN, { conf_id: CID, imsi: IMSI.ue3 }),
  ])
  const c = mid[0]
  check('conf active after START+JOINs', mid.length === 1 && c.active)
  check('host tracked (IMSI)', c.host === IMSI.ue1)
  check('3 members present (host + 2 joins)',
    c.members[IMSI.ue1]?.present === true && c.members[IMSI.ue2]?.present === true && c.members[IMSI.ue3]?.present === true)
  check('confUeIds maps members to UEs',
    JSON.stringify(confUeIds(c, { ...IMSI })) === '["ue1","ue2","ue3"]', JSON.stringify(confUeIds(c, { ...IMSI })))

  const after = deriveConfs([
    ev('ue1', svc.CONF_START, { host: IMSI.ue1, conf_id: CID }),
    ev('ue2', svc.CONF_JOIN, { conf_id: CID, imsi: IMSI.ue2 }),
    ev('ue3', svc.CONF_JOIN, { conf_id: CID, imsi: IMSI.ue3 }),
    ev('ue3', svc.CONF_LEAVE, { conf_id: CID, imsi: IMSI.ue3, reason: 'hangup' }),
  ])[0]
  check('leave marks member gone with reason, conf stays active',
    after.active && after.members[IMSI.ue3]?.present === false && after.members[IMSI.ue3]?.reason === 'hangup')
  check('confUeIds drops the leaver', JSON.stringify(confUeIds(after, { ...IMSI })) === '["ue1","ue2"]')

  const end = deriveConfs([
    ev('ue1', svc.CONF_START, { host: IMSI.ue1, conf_id: CID }),
    ev('ue2', svc.CONF_JOIN, { conf_id: CID, imsi: IMSI.ue2 }),
    ev('ue1', svc.CONF_LEAVE, { conf_id: CID, imsi: IMSI.ue1, reason: 'host' }),
    ev('ue1', svc.CONF_END, { conf_id: CID, reason: 'host' }),
  ])[0]
  check('CONF_END ends the conference with reason', !end.active && end.endReason === 'host')
  check('CONF_END marks every member not present',
    Object.values(end.members).every((m) => !m.present))
}

console.log('deriveConfs — window tolerance / out-of-order')
{
  const oo = deriveConfs([
    ev('ue2', svc.CONF_JOIN, { conf_id: '7001', imsi: IMSI.ue2 }), // JOIN before START
    ev('ue1', svc.CONF_START, { host: IMSI.ue1, conf_id: '7001' }),
  ])
  check('JOIN before START tolerated (member kept, host filled)',
    oo.length === 1 && oo[0].active && oo[0].host === IMSI.ue1 && oo[0].members[IMSI.ue2]?.present === true)
  const unknown = deriveConfs([
    ev('ue3', svc.CONF_LEAVE, { conf_id: '9999', imsi: IMSI.ue3, reason: 'hangup' }),
    ev('ue3', svc.CONF_END, { conf_id: '9999', reason: 'empty' }),
  ])
  check('LEAVE/END for unknown conf_id are dropped', unknown.length === 0)
}

console.log('deriveCalls — kind="conf" keyed separately, CONF_* lifecycle')
{
  const imsiMap: Record<UeId, string | null> = { ...IMSI }
  const CID = '9001'
  // voice leg (SIP dialog) + conf stream stats coexist on the same UE
  const live = deriveCalls([
    ev('ue1', svc.SIP_INVITE_TX, { dst: IMSI.ue2, kind: 'voice' }),
    ev('ue2', svc.SIP_INVITE_RX, { src: IMSI.ue1, kind: 'voice' }),
    ev('ue1', svc.CONF_START, { host: IMSI.ue1, conf_id: CID }),
    ev('ue2', svc.SIP_CALL_ESTABLISHED, { peer: IMSI.ue1, kind: 'voice' }),
    ev('ue2', svc.CONF_JOIN, { conf_id: CID, imsi: IMSI.ue2 }),
    ev('ue1', svc.APP_STREAM_STATS, { kind: 'conf', qci: '1', conf_id: CID, tx: '30', rx: '29', loss: '0', rtt_avg: '14' }),
    ev('ue2', svc.APP_STREAM_STATS, { kind: 'conf', qci: '1', conf_id: CID, tx: '15', rx: '14', loss: '0', rtt_avg: '15' }),
  ], imsiMap)
  check('conf stats key their own CallMap slot', !!live.ue1.conf && live.ue1.conf!.established)
  check('conf stats carry qci=1', live.ue1.conf?.stats?.qci === '1')
  check('voice leg stays separate from conf stream', !!live.ue1.voice && !live.ue2.voice?.stats)

  // ue2 leaves: conf slot closes, voice leg survives until BYE
  const left = deriveCalls([
    ...([] as LogEvent[]),
    ev('ue2', svc.SIP_INVITE_RX, { src: IMSI.ue1, kind: 'voice' }),
    ev('ue2', svc.APP_STREAM_STATS, { kind: 'conf', qci: '1', conf_id: CID, tx: '5', rx: '5', loss: '0', rtt_avg: '15' }),
    ev('bs', svc.CONF_LEAVE, { conf_id: CID, imsi: IMSI.ue2, reason: 'hangup' }), // fired off-UE: IMSI-mapped
    ev('ue2', svc.APP_STREAM_STATS, { kind: 'conf', qci: '1', conf_id: CID, tx: '6', rx: '6', loss: '0', rtt_avg: '15' }), // must not revive
  ], imsiMap)
  check('CONF_LEAVE closes the conf slot (IMSI-resolved)', !left.ue2.conf)
  check('CONF_LEAVE blocks conf stats-revival', !left.ue2.conf)
  check('voice leg survives CONF_LEAVE', !!left.ue2.voice)

  // CONF_END closes conf on every UE; without an IMSI map, node fallback works
  const endedAll = deriveCalls([
    ev('ue1', svc.APP_STREAM_STATS, { kind: 'conf', qci: '1', conf_id: CID, tx: '9', rx: '9', loss: '0', rtt_avg: '14' }),
    ev('ue2', svc.APP_STREAM_STATS, { kind: 'conf', qci: '1', conf_id: CID, tx: '9', rx: '9', loss: '0', rtt_avg: '15' }),
    ev('ue1', svc.CONF_END, { conf_id: CID, reason: 'host' }),
  ], imsiMap)
  check('CONF_END closes conf on all UEs', !endedAll.ue1.conf && !endedAll.ue2.conf)
  const nodeFallback = deriveCalls([
    ev('ue3', svc.APP_STREAM_STATS, { kind: 'conf', qci: '1', conf_id: CID, tx: '2', rx: '2', loss: '0', rtt_avg: '16' }),
    ev('ue3', svc.CONF_LEAVE, { conf_id: CID, imsi: IMSI.ue3, reason: 'hangup' }), // no map: own node
  ])
  check('CONF_LEAVE without IMSI map falls back to event node', !nodeFallback.ue3.conf)
}

console.log('deriveRadio — latest quality/MCS/power per UE, c_rnti mapping')
{
  const r = deriveRadio([
    ev('ue1', svc.LINK_QUALITY, { c_rnti: CRNTI.ue1, rsrp: '-80', sinr: '26' }),
    ev('ue1', svc.LINK_QUALITY, { c_rnti: CRNTI.ue1, rsrp: '-78', sinr: '28' }), // latest wins
    ev('bs', svc.MCS_CHANGE, { c_rnti: CRNTI.ue1, mcs: '16qam', direction: 'dl' }), // BS-side, mapped
    ev('bs', svc.CQI_REPORT, { c_rnti: CRNTI.ue1, cqi: '14', snr_db: '28' }),
    ev('ue3', svc.TX_POWER_CHANGE, { c_rnti: CRNTI.ue3, dbm: '3' }),
    ev('bs', svc.MCS_CHANGE, { c_rnti: '9999', mcs: '64qam', direction: 'dl' }), // unknown: dropped
    ev('bs', svc.LINK_QUALITY, { c_rnti: CRNTI.ue2, rsrp: '-85', sinr: '21' }), // mapped via crnti too
  ], CRNTI)
  check('latest LINK_QUALITY wins', r.ue1.sinr === 28 && r.ue1.rsrp === -78)
  check('BS-side MCS_CHANGE mapped via c_rnti', r.ue1.mcs === '16qam')
  check('BS-side CQI_REPORT mapped via c_rnti', r.ue1.cqi === 14)
  check('UE-side TX_POWER_CHANGE tracked', r.ue3.txDbm === 3)
  check('BS-node LINK_QUALITY resolves via c_rnti', r.ue2.sinr === 21)
  check('unknown c_rnti dropped', r.ue2.mcs === null && r.ue3.mcs === null)

  const empty = deriveRadio([], CRNTI)
  check('no events → all null (no bars/badges)',
    empty.ue1.sinr === null && empty.ue1.mcs === null && empty.ue1.txDbm === null && empty.ue1.cqi === null)
}

console.log('sinrBars / qualityTier / mcsLabel')
{
  check('bars: 28dB→4, 21→3, 19→2 (demo links split)', sinrBars(28) === 4 && sinrBars(21) === 3 && sinrBars(19) === 2)
  check('bars: 10→1, 3→0, null→0', sinrBars(10) === 1 && sinrBars(3) === 0 && sinrBars(null) === 0)
  check('bars boundary: 25→4, 20→3, 15→2, 8→1', sinrBars(25) === 4 && sinrBars(20) === 3 && sinrBars(15) === 2 && sinrBars(8) === 1)
  check('tiers: good/mid/poor/null', qualityTier(28) === 'good' && qualityTier(21) === 'mid' && qualityTier(19) === 'poor' && qualityTier(null) === null)
  check('mcsLabel uppercases, null passthrough', mcsLabel('16qam') === '16QAM' && mcsLabel('qpsk') === 'QPSK' && mcsLabel(null) === null)
}

console.log('deriveSleep — inactive/resume/paging tracking')
{
  const imsiMap: Record<UeId, string | null> = { ...IMSI }
  const s = deriveSleep([
    ev('ue3', svc.RRC_INACTIVE, { c_rnti: CRNTI.ue3, resume_id: '813' }), // UE-side
    ev('bs', svc.RRC_INACTIVE, { c_rnti: CRNTI.ue2, resume_id: '457' }), // BS-side, crnti-mapped
    ev('bs', svc.RRC_INACTIVE, { c_rnti: '9999', resume_id: '1' }), // unknown: dropped
    ev('ue3', svc.RRC_RESUME_REQUEST, { resume_id: '813' }),
    ev('bs', svc.PAGE_TX, { imsi: IMSI.ue3 }), // paging via IMSI map
    ev('ue2', svc.PAGE_RX, { imsi: IMSI.ue2 }), // paging on the UE node itself
  ], CRNTI, imsiMap)
  check('UE-side RRC_INACTIVE suspends', s.ue3.inactive && s.ue3.inactiveAt > 0)
  check('BS-side RRC_INACTIVE mapped via c_rnti', s.ue2.inactive)
  check('unknown c_rnti dropped', !s.ue1.inactive)
  check('RESUME_REQUEST marks resuming (still inactive)', s.ue3.resuming && s.ue3.inactive)
  check('PAGE_TX mapped via IMSI', s.ue3.pagedAt > 0)
  check('PAGE_RX on UE node tracked', s.ue2.pagedAt > 0)

  const w = deriveSleep([
    ev('ue3', svc.RRC_INACTIVE, { c_rnti: CRNTI.ue3, resume_id: '813' }),
    ev('ue3', svc.RRC_RESUME_REQUEST, { resume_id: '813' }),
    ev('ue3', svc.RRC_RESUMED, { c_rnti: CRNTI.ue3, old_c_rnti: CRNTI.ue3 }),
  ], CRNTI, imsiMap)
  check('RRC_RESUMED clears inactive, keeps resumedAt', !w.ue3.inactive && !w.ue3.resuming && w.ue3.resumedAt > 0)

  const fail = deriveSleep([
    ev('ue3', svc.RRC_INACTIVE, { c_rnti: CRNTI.ue3, resume_id: '813' }),
    ev('ue3', svc.RRC_RESUME_REQUEST, { resume_id: '813' }),
    ev('bs', svc.RRC_RESUME_FAIL, { resume_id: '813', reason: 'unknown' }),
  ], CRNTI, imsiMap)
  check('RESUME_FAIL keeps inactive, clears resuming', fail.ue3.inactive && !fail.ue3.resuming)
}

console.log('deriveSleep — RRC_UE_STATE fallback / window tolerance')
{
  const imsiMap: Record<UeId, string | null> = { ...IMSI }
  const viaState = deriveSleep([
    ev('ue2', 'RRC_UE_STATE', { old: 'CONNECTED', new: 'INACTIVE' }),
  ], CRNTI, imsiMap)
  check('RRC_UE_STATE INACTIVE suspends', viaState.ue2.inactive)
  const back = deriveSleep([
    ev('ue2', svc.RRC_INACTIVE, { c_rnti: CRNTI.ue2, resume_id: '5' }),
    ev('ue2', 'RRC_UE_STATE', { old: 'INACTIVE', new: 'CONNECTED' }), // RESUMED slid out
  ], CRNTI, imsiMap)
  check('RRC_UE_STATE CONNECTED revives (tolerant to lost RESUMED)', !back.ue2.inactive)
  const lone = deriveSleep([
    ev('ue1', svc.RRC_RESUMED, { c_rnti: CRNTI.ue1, old_c_rnti: CRNTI.ue1 }), // no INACTIVE in window
  ], CRNTI, imsiMap)
  check('lone RESUMED is a no-op state-wise', !lone.ue1.inactive && lone.ue1.resumedAt > 0)
  const none = deriveSleep([], CRNTI, imsiMap)
  check('no events → all awake, no paging', !none.ue1.inactive && none.ue2.pagedAt === 0 && none.ue3.resumedAt === 0)
}

console.log('deriveAuth — AKA exchange state (P7 + M15 families)')
{
  const imsiMap: Record<UeId, string | null> = { ...IMSI }
  const okFlow = deriveAuth([
    ev('bs', svc.NAS_AUTH_VECTOR, { imsi: IMSI.ue1, rand: 'a3f19c2e', sqn_masked: '0000**31' }),
    ev('ue1', svc.NAS_AUTH_RES, { imsi: IMSI.ue1, res: '4d9bc231' }),
    ev('bs', svc.NAS_AUTH_SUCCESS, { imsi: IMSI.ue1 }),
  ], imsiMap)
  check('P7 VECTOR→RES→SUCCESS progression', okFlow.ue1.step === 'success' && okFlow.ue1.okAt > 0)

  const m15 = deriveAuth([
    ev('bs', svc.NAS_AUTH_CHALLENGE_TX, { imsi: IMSI.ue2, tmsi: '70002' }),
    ev('ue2', svc.NAS_AUTH_RESPONSE_TX, {}), // field-less, resolved by node
    ev('bs', svc.NAS_AUTH_OK, { imsi: IMSI.ue2 }),
  ], imsiMap)
  check('M15 family tolerated (CHALLENGE/RESPONSE/OK)', m15.ue2.step === 'success')

  const retry = deriveAuth([
    ev('bs', svc.NAS_AUTH_VECTOR, { imsi: IMSI.ue3, rand: '11', sqn_masked: 'x' }),
    ev('ue3', svc.NAS_AUTH_RES, { imsi: IMSI.ue3, res: '22' }),
    ev('bs', svc.NAS_AUTH_FAIL, { imsi: IMSI.ue3, cause: 'mac' }),
    ev('bs', svc.NAS_AUTH_VECTOR, { imsi: IMSI.ue3, rand: '33', sqn_masked: 'y' }), // retry resets terminal state
    ev('ue3', svc.NAS_AUTH_RES, { imsi: IMSI.ue3, res: '44' }),
    ev('bs', svc.NAS_AUTH_SUCCESS, { imsi: IMSI.ue3 }),
  ], imsiMap)
  check('fail→retry→success ends in success', retry.ue3.step === 'success' && retry.ue3.failAt > 0)

  const failed = deriveAuth([
    ev('bs', svc.NAS_AUTH_VECTOR, { imsi: IMSI.ue2, rand: 'aa', sqn_masked: 'z' }),
    ev('bs', svc.NAS_AUTH_FAIL, { cause: 'synch' }), // field-less BS form: single in-flight fallback
  ], imsiMap)
  check('field-less BS FAIL attributed to the in-flight UE', failed.ue2.step === 'fail' && failed.ue2.cause === 'synch')
  check('AUTH_CAUSE maps the wire causes', AUTH_CAUSE.mac === 'MAC 校验失败' && AUTH_CAUSE.synch === 'SQN 失步' && AUTH_CAUSE.res_mismatch === 'RES 不匹配')

  const unknown = deriveAuth([
    ev('bs', svc.NAS_AUTH_VECTOR, { imsi: '999999999999999', rand: 'aa', sqn_masked: 'z' }), // unknown IMSI
    ev('ue1', svc.NAS_AUTH_SUCCESS, { imsi: IMSI.ue1 }), // lone terminal: UE node resolves
  ], imsiMap)
  check('unknown IMSI dropped', unknown.ue2.step === null && unknown.ue3.step === null)
  check('lone SUCCESS tolerated', unknown.ue1.step === 'success')
  const none = deriveAuth([], imsiMap)
  check('no auth events → all null (beat never renders)', none.ue1.step === null && none.ue1.okAt === 0 && none.ue1.failAt === 0)
}

console.log('deriveMobility — serving cell + handover state')
{
  const imsiMap: Record<UeId, string | null> = { ...IMSI }
  const start = deriveMobility([
    ev('bs', svc.HANDOVER_START, { imsi: IMSI.ue1, from: '1', to: '2' }),
  ], imsiMap, CRNTI)
  check('HANDOVER_START: in-flight, serving stays on source', start.ue1.inFlight && start.ue1.serving === '1')
  check('HANDOVER_START: from/to tracked', start.ue1.hoFrom === '1' && start.ue1.hoTo === '2')

  const done = deriveMobility([
    ev('bs', svc.HANDOVER_START, { imsi: IMSI.ue1, from: '1', to: '2' }),
    ev('bs2', svc.HANDOVER_DONE, { imsi: IMSI.ue1, from: '1', to: '2', path: 'ho' }),
  ], imsiMap, CRNTI)
  check('HANDOVER_DONE: serving flips, flight ends', !done.ue1.inFlight && done.ue1.serving === '2')
  check('HANDOVER_DONE: path + doneAt recorded', done.ue1.hoPath === 'ho' && done.ue1.hoDoneAt > 0)
  check('other UEs untouched', done.ue2.serving === '1' && !done.ue2.inFlight)

  // M14 family: HO_COMMAND_TX / HO_COMPLETE_RX with rnti mapping
  const m14 = deriveMobility([
    ev('bs', svc.HO_COMMAND_TX, { cell: '2', rnti: CRNTI.ue3 }),
    ev('bs2', svc.HO_COMPLETE_RX, { cell: '2', rnti: CRNTI.ue3 }),
  ], imsiMap, CRNTI)
  check('M14 COMMAND/COMPLETE tolerated (rnti-mapped)', m14.ue3.serving === '2' && !m14.ue3.inFlight)

  check('cellOf normalizes ids', cellOf('bs2') === '2' && cellOf('gnb1') === '1' && cellOf('2') === '2' && cellOf('x') === null)
  check('nodeOf resolves bs2', nodeOf(ev('bs2', 'X', {})) === 'bs2')

  const single = deriveMobility([ev('ue1', svc.APP_MSG_TX, { dst: IMSI.ue2, text: 'hi' })], imsiMap, CRNTI)
  check('single-BS stream: all defaults (cell 1, no HO)',
    single.ue1.serving === '1' && !single.ue1.inFlight && single.ue1.hoDoneAt === 0)
  const unknown = deriveMobility([
    ev('bs', svc.HANDOVER_START, { imsi: '999999999999999', from: '1', to: '2' }),
  ], imsiMap, CRNTI)
  check('unknown IMSI dropped', UE_NODES_EVERY(unknown))
}
function UE_NODES_EVERY(m: Record<UeId, { serving: string; inFlight: boolean }>): boolean {
  return (['ue1', 'ue2', 'ue3'] as UeId[]).every((id) => m[id].serving === '1' && !m[id].inFlight)
}

console.log('deriveCalls — voice call rides through handover events')
{
  const imsiMap: Record<UeId, string | null> = { ...IMSI }
  const calls = deriveCalls([
    ev('ue1', svc.SIP_INVITE_TX, { dst: IMSI.ue2, kind: 'voice' }),
    ev('ue2', svc.SIP_INVITE_RX, { src: IMSI.ue1, kind: 'voice' }),
    ev('ue2', svc.SIP_CALL_ESTABLISHED, { peer: IMSI.ue1, kind: 'voice' }),
    ev('ue1', svc.SIP_CALL_ESTABLISHED, { peer: IMSI.ue2, kind: 'voice' }),
    ev('ue1', svc.APP_STREAM_STATS, { kind: 'voice', peer: IMSI.ue2, tx: '10', rx: '9', loss: '0', rtt_avg: '18' }),
    ev('bs', svc.HANDOVER_START, { imsi: IMSI.ue1, from: '1', to: '2' }), // HO mid-call
    ev('bs2', svc.HANDOVER_DONE, { imsi: IMSI.ue1, from: '1', to: '2', path: 'ho' }),
    ev('ue1', svc.APP_STREAM_STATS, { kind: 'voice', peer: IMSI.ue2, tx: '20', rx: '19', loss: '0', rtt_avg: '17' }),
  ], imsiMap)
  check('call state ignores HO events (no reset)',
    !!calls.ue1.voice && calls.ue1.voice!.established && calls.ue1.voice!.stats?.tx === '20')
}

console.log(`\n${passed} passed, ${failed} failed`)
process.exit(failed > 0 ? 1 : 0)