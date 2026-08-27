import { LogEvent } from './hooks/useWebSocket'

/** Canonical node identities in a multi-UE deployment. */
export type NodeId = 'ue1' | 'ue2' | 'ue3' | 'bs' | 'bs2'

/** UE nodes (everything that is not a BS). */
export type UeId = Exclude<NodeId, 'bs' | 'bs2'>

export const UE_NODES: UeId[] = ['ue1', 'ue2', 'ue3']

export function isUe(n: NodeId | null): n is UeId {
  return n === 'ue1' || n === 'ue2' || n === 'ue3'
}

export function isBs(n: NodeId | null): n is 'bs' | 'bs2' {
  return n === 'bs' || n === 'bs2'
}

/**
 * Resolve an event's node. New streams carry a top-level `node` field
 * ("ue1" | "ue2" | "ue3" | "bs" | "bs2"); old single-UE streams only have
 * `module`, where "UE" maps to ue1 and "BS" to bs so they keep rendering
 * sensibly. A dual-BS (P8) stream names the second tower "bs2".
 */
export function nodeOf(msg: LogEvent): NodeId | null {
  const n = (msg.node || '').toLowerCase()
  if (n === 'ue1' || n === 'ue') return 'ue1'
  if (n === 'ue2') return 'ue2'
  if (n === 'ue3') return 'ue3'
  if (n === 'bs' || n === 'gnb') return 'bs'
  if (n === 'bs2' || n === 'gnb2') return 'bs2'
  if (msg.module === 'UE') return 'ue1'
  if (msg.module === 'BS') return 'bs'
  return null
}

export const NODE_LABEL: Record<NodeId, string> = {
  ue1: 'UE1',
  ue2: 'UE2',
  ue3: 'UE3',
  bs: 'gNB',
  bs2: 'gNB2',
}

export const NODE_COLOR: Record<NodeId, string> = {
  ue1: '#34d399',
  ue2: '#8b5cf6',
  ue3: '#f59e0b',
  bs: '#60a5fa',
  bs2: '#818cf8',
}
