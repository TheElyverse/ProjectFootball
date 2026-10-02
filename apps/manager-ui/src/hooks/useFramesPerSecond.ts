import { useEffect, useState } from "react";

// Frames the page painted in the last second, for spotting UI performance problems.
export function useFramesPerSecond(): number {
  const [framesPerSecond, setFramesPerSecond] = useState(0);
  useEffect(() => {
    let frames = 0;
    let windowStart = performance.now();
    let handle = requestAnimationFrame(function tick(now) {
      frames += 1;
      if (now - windowStart >= 1000) {
        setFramesPerSecond(Math.round((frames * 1000) / (now - windowStart)));
        frames = 0;
        windowStart = now;
      }
      handle = requestAnimationFrame(tick);
    });
    return () => cancelAnimationFrame(handle);
  }, []);
  return framesPerSecond;
}
