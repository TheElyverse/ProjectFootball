// Small progressive enhancements for the static page. The page is complete
// without this script; it only adds the sticky header style, the mobile menu,
// scroll reveals and the animated pitch in the hero.

const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

// ---------------------------------------------------------------- Header ---

const header = document.getElementById("site-header");

function updateHeader() {
  header?.toggleAttribute("data-scrolled", window.scrollY > 16);
}

window.addEventListener("scroll", updateHeader, { passive: true });
updateHeader();

// ----------------------------------------------------------- Mobile menu ---

const menuToggle = document.getElementById("menu-toggle");
const mobileMenu = document.getElementById("mobile-menu");

function setMenuOpen(open) {
  menuToggle?.setAttribute("aria-expanded", String(open));
  mobileMenu?.classList.toggle("hidden", !open);
  if (open) header?.setAttribute("data-scrolled", "");
  else updateHeader();
}

menuToggle?.addEventListener("click", () => {
  setMenuOpen(menuToggle.getAttribute("aria-expanded") !== "true");
});
mobileMenu?.querySelectorAll("a").forEach((link) => {
  link.addEventListener("click", () => setMenuOpen(false));
});

// --------------------------------------------------------- Scroll reveal ---

const revealObserver = new IntersectionObserver(
  (entries) => {
    for (const entry of entries) {
      if (entry.isIntersecting) {
        entry.target.classList.add("is-visible");
        revealObserver.unobserve(entry.target);
      }
    }
  },
  { rootMargin: "0px 0px -10% 0px" },
);
document.querySelectorAll("[data-reveal]").forEach((element) => revealObserver.observe(element));

document.querySelectorAll("[data-year]").forEach((element) => {
  element.textContent = String(new Date().getFullYear());
});

// ------------------------------------------------------------ Hero pitch ---
//
// A tiny, purely decorative passing move on a 105 m × 68 m pitch. The player on
// the ball turns to look for teammates (his field of view), weighs the ones he
// can see and picks one, like the real match engine does -- only much simpler.
// It uses a seeded random generator, so the move is the same on every visit.

const svgNamespace = "http://www.w3.org/2000/svg";
const pitch = document.getElementById("hero-pitch");

if (pitch) {
  startHeroPitch(pitch);
}

function startHeroPitch(svg) {
  const cone = svg.getElementById("hero-cone");
  const options = svg.getElementById("hero-options");
  const passLine = svg.getElementById("hero-pass");
  const ball = svg.getElementById("hero-ball");
  const clock = document.getElementById("hero-clock");
  const decision = document.getElementById("hero-decision");

  // Seeded linear congruential generator: deterministic, like the simulation.
  let seed = 7;
  const random = () => {
    seed = (seed * 1664525 + 1013904223) >>> 0;
    return seed / 2 ** 32;
  };

  const makeTeam = (team, positions) =>
    positions.map(([x, y, number], index) => ({
      team,
      number,
      baseX: x,
      baseY: y,
      x,
      y,
      phase: index * 1.7,
    }));

  const home = makeTeam("home", [
    [6, 34, 1],
    [24, 17, 4],
    [24, 51, 5],
    [38, 34, 6],
    [53, 15, 8],
    [53, 53, 10],
    [68, 34, 9],
  ]);
  const away = makeTeam("away", [
    [99, 34, 1],
    [84, 22, 2],
    [84, 46, 3],
    [72, 34, 6],
    [62, 22, 7],
    [62, 46, 11],
    [48, 34, 9],
  ]);
  const players = [...home, ...away];

  // One <g> per player: a disc and his shirt number.
  for (const player of players) {
    const group = document.createElementNS(svgNamespace, "g");
    const disc = document.createElementNS(svgNamespace, "circle");
    disc.setAttribute("r", "1.9");
    disc.setAttribute("fill", player.team === "home" ? "#d4ff4f" : "#ff7b67");
    disc.setAttribute("stroke", "#040907");
    disc.setAttribute("stroke-width", "0.3");
    const label = document.createElementNS(svgNamespace, "text");
    label.textContent = String(player.number);
    label.setAttribute("text-anchor", "middle");
    label.setAttribute("dominant-baseline", "central");
    label.setAttribute("font-size", "1.9");
    label.setAttribute("font-weight", "700");
    label.setAttribute("font-family", "system-ui, sans-serif");
    label.setAttribute("fill", "#040907");
    group.append(disc, label);
    svg.getElementById("hero-players").append(group);
    player.element = group;
  }

  const distance = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);
  const angleTo = (from, to) => Math.atan2(to.y - from.y, to.x - from.x);
  const shortestTurn = (from, to) => Math.atan2(Math.sin(to - from), Math.cos(to - from));
  const easeInOut = (t) => (t < 0.5 ? 2 * t * t : 1 - (-2 * t + 2) ** 2 / 2);

  // Passing options of the carrier, each with a utility: forward passes, a
  // comfortable distance and space around the receiver are worth more.
  function passOptions(carrier) {
    const candidates = home
      .filter((mate) => mate !== carrier && mate.number !== 1)
      .map((mate) => {
        const length = distance(carrier, mate);
        const space = Math.min(...away.map((opponent) => distance(opponent, mate)));
        const forward = (mate.x - carrier.x) / 40;
        const score = Math.max(0.05, 1 - Math.abs(length - 20) / 25 + forward + Math.min(space, 12) / 12);
        return { mate, score };
      })
      .filter((option) => distance(carrier, option.mate) < 42);
    const total = candidates.reduce((sum, option) => sum + option.score, 0);
    return candidates.map((option) => ({ ...option, utility: option.score / total }));
  }

  function choose(candidates) {
    let roll = random();
    for (const option of candidates) {
      roll -= option.utility;
      if (roll <= 0) return option;
    }
    return candidates[candidates.length - 1];
  }

  const holdSeconds = 1.5;
  const state = {
    carrier: home[3],
    options: [],
    receiver: null,
    phase: "hold",
    elapsed: 0,
    facing: 0,
    facingFrom: 0,
    passFrom: { x: 0, y: 0 },
    travelSeconds: 1,
    matchSeconds: 23 * 60 + 12,
  };

  function startHold() {
    state.phase = "hold";
    state.elapsed = 0;
    state.options = passOptions(state.carrier);
    const choice = choose(state.options);
    state.receiver = choice.mate;
    state.facingFrom = state.facing;
    if (decision) {
      decision.textContent = `#${state.carrier.number} → #${choice.mate.number} · ${choice.utility.toFixed(2)}`;
    }
  }

  function movePlayers(time, ballX) {
    // Both teams shift with the ball; everybody drifts a little around his spot.
    for (const player of players) {
      const shift = (ballX - 52.5) * (player.team === "home" ? 0.25 : 0.2);
      const keeper = player.number === 1;
      player.x = player.baseX + (keeper ? shift * 0.1 : shift) + Math.sin(time * 0.6 + player.phase) * 1.6;
      player.y = player.baseY + Math.cos(time * 0.45 + player.phase) * 1.8;
    }
    // The nearest opponent presses the player on the ball.
    const presser = away
      .filter((opponent) => opponent.number !== 1)
      .reduce((best, opponent) => (distance(opponent, state.carrier) < distance(best, state.carrier) ? opponent : best));
    const pressure = state.phase === "hold" ? easeInOut(Math.min(1, state.elapsed / holdSeconds)) * 0.55 : 0.2;
    presser.x += (state.carrier.x + 3 - presser.x) * pressure;
    presser.y += (state.carrier.y - presser.y) * pressure;
  }

  function coneTo(from, angle, radius, halfAngle) {
    const leftX = from.x + Math.cos(angle - halfAngle) * radius;
    const leftY = from.y + Math.sin(angle - halfAngle) * radius;
    const rightX = from.x + Math.cos(angle + halfAngle) * radius;
    const rightY = from.y + Math.sin(angle + halfAngle) * radius;
    return `M${from.x} ${from.y} L${leftX} ${leftY} A${radius} ${radius} 0 0 1 ${rightX} ${rightY} Z`;
  }

  function render() {
    for (const player of players) {
      player.element.setAttribute("transform", `translate(${player.x.toFixed(2)} ${player.y.toFixed(2)})`);
    }

    const carrier = state.carrier;
    let ballX;
    let ballY;
    if (state.phase === "hold") {
      ballX = carrier.x + Math.cos(state.facing) * 2.4;
      ballY = carrier.y + Math.sin(state.facing) * 2.4;
      cone.setAttribute("d", coneTo(carrier, state.facing, 26, 0.8));
      cone.setAttribute("opacity", "1");
      const lookedAt = state.elapsed / holdSeconds;
      options.replaceChildren(
        ...state.options
          .filter(() => lookedAt > 0.35)
          .map((option) => {
            const line = document.createElementNS(svgNamespace, "line");
            line.setAttribute("x1", carrier.x.toFixed(2));
            line.setAttribute("y1", carrier.y.toFixed(2));
            line.setAttribute("x2", option.mate.x.toFixed(2));
            line.setAttribute("y2", option.mate.y.toFixed(2));
            return line;
          }),
      );
      passLine.setAttribute("opacity", "0");
    } else {
      const t = Math.min(1, state.elapsed / state.travelSeconds);
      const eased = 1 - (1 - t) ** 2; // the ball slows down as it rolls
      ballX = state.passFrom.x + (state.receiver.x - state.passFrom.x) * eased;
      ballY = state.passFrom.y + (state.receiver.y - state.passFrom.y) * eased;
      cone.setAttribute("opacity", String(Math.max(0, 1 - t * 3)));
      options.replaceChildren();
      passLine.setAttribute("x1", ballX.toFixed(2));
      passLine.setAttribute("y1", ballY.toFixed(2));
      passLine.setAttribute("x2", state.receiver.x.toFixed(2));
      passLine.setAttribute("y2", state.receiver.y.toFixed(2));
      passLine.setAttribute("opacity", "0.9");
    }
    ball.setAttribute("cx", ballX.toFixed(2));
    ball.setAttribute("cy", ballY.toFixed(2));

    if (clock) {
      const minutes = Math.floor(state.matchSeconds / 60);
      const seconds = Math.floor(state.matchSeconds % 60);
      clock.textContent = `${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}`;
    }
    return ballX;
  }

  function step(deltaSeconds, time) {
    state.elapsed += deltaSeconds;
    state.matchSeconds += deltaSeconds * 4;

    if (state.phase === "hold") {
      // Scan: glance away from the target first, then settle on it.
      const target = angleTo(state.carrier, state.receiver);
      const t = Math.min(1, state.elapsed / holdSeconds);
      const glance = Math.sin(t * Math.PI) * 0.9;
      state.facing = state.facingFrom + shortestTurn(state.facingFrom, target) * easeInOut(t) + glance * (1 - t);
      if (state.elapsed >= holdSeconds) {
        state.phase = "travel";
        state.elapsed = 0;
        state.passFrom = {
          x: state.carrier.x + Math.cos(state.facing) * 2.4,
          y: state.carrier.y + Math.sin(state.facing) * 2.4,
        };
        state.travelSeconds = Math.min(1.4, Math.max(0.6, distance(state.carrier, state.receiver) / 24));
      }
    } else if (state.elapsed >= state.travelSeconds) {
      state.facing = angleTo(state.carrier, state.receiver);
      state.carrier = state.receiver;
      startHold();
    }

    const ballX = state.phase === "hold" ? state.carrier.x : state.passFrom.x;
    movePlayers(time, ballX);
  }

  startHold();
  step(0, 0);
  render();

  if (reducedMotion) {
    // A single still frame: the carrier looking at his options.
    state.elapsed = holdSeconds * 0.8;
    step(0, 0);
    render();
    return;
  }

  let visible = true;
  let last = performance.now();
  new IntersectionObserver(([entry]) => {
    visible = entry.isIntersecting;
    last = performance.now();
  }).observe(svg);

  function frame(now) {
    if (visible && !document.hidden) {
      const delta = Math.min(0.05, (now - last) / 1000);
      step(delta, now / 1000);
      render();
    }
    last = now;
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
}
