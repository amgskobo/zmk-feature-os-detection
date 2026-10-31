import { useCallback, useEffect, useRef, useState } from "react";

// Reset the panel's complete state when its connection or subsystem changes.
export function useConnectionKey(
  connection: unknown,
  index: number | undefined
) {
  const [source, setSource] = useState({ connection, index, key: 0 });
  if (source.connection !== connection || source.index !== index) {
    const next = { connection, index, key: source.key + 1 };
    setSource(next);
    return next.key;
  }
  return source.key;
}

// Polls admit only while idle. A foreground operation may retire an active poll;
// only the current ticket can finish admission, including after a late reply.
export function useRequestLifetime() {
  const lifetime = useRef({
    active: true,
    generation: 0,
    pending: null as number | null,
  });
  useEffect(() => {
    const current = lifetime.current;
    current.active = true;
    return () => {
      current.active = false;
      ++current.generation;
      current.pending = null;
    };
  }, []);
  return useCallback((kind: "poll" | "foreground" = "foreground") => {
    const current = lifetime.current;
    if (!current.active || (kind === "poll" && current.pending !== null))
      return null;
    const generation = ++current.generation;
    current.pending = generation;
    return {
      active: () => current.active && generation === current.generation,
      finish: () => {
        if (current.pending === generation) current.pending = null;
      },
    };
  }, []);
}
