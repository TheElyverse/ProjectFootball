import { useEffect, useMemo, useRef, useState } from "react";
import { useIntl, type IntlShape } from "react-intl";
import type { SquadRow, SquadView } from "@/ue/bridge";

// Virtualized like the UMG ListView and the Slate SListView: only the visible rows
// plus a few above and below are rendered.
const ROW_HEIGHT = 24;
const OVERSCAN = 5;

interface Column {
  id: string;
  label: string;
  width: number;
  text: (row: SquadRow) => string;
  value: (row: SquadRow) => number | string;
}

function buildColumns(attributeNames: string[], intl: IntlShape): Column[] {
  const numeric = (
    id: string,
    label: string,
    value: (row: SquadRow) => number,
  ): Column => ({
    id,
    label,
    width: 72,
    text: (row) => String(value(row)),
    value,
  });
  return [
    {
      id: "name",
      label: intl.formatMessage({ id: "squad.name" }),
      width: 160,
      text: (row) => row.name,
      value: (row) => row.name,
    },
    {
      id: "position",
      label: intl.formatMessage({ id: "squad.position" }),
      width: 72,
      text: (row) => row.position,
      value: (row) => row.position,
    },
    numeric("age", intl.formatMessage({ id: "squad.age" }), (row) => row.age),
    ...attributeNames.map((label, index) =>
      numeric(`attribute${index}`, label, (row) => row.attributes[index] ?? 0),
    ),
    numeric(
      "fitness",
      intl.formatMessage({ id: "squad.fitness" }),
      (row) => row.fitness,
    ),
    numeric(
      "contract",
      intl.formatMessage({ id: "squad.contract" }),
      (row) => row.contractYears,
    ),
    {
      ...numeric(
        "value",
        intl.formatMessage({ id: "squad.value" }),
        (row) => row.marketValue,
      ),
      width: 120,
      text: (row) =>
        intl.formatNumber(row.marketValue, {
          style: "currency",
          currency: "EUR",
          maximumFractionDigits: 0,
        }),
    },
  ];
}

interface Sort {
  column: Column;
  ascending: boolean;
}

export function SquadTable({ squad }: { squad: SquadView }) {
  const intl = useIntl();
  const columns = useMemo(
    () => buildColumns(squad.attributeNames, intl),
    [squad.attributeNames, intl],
  );
  const [sort, setSort] = useState<Sort | undefined>();
  const [scrollTop, setScrollTop] = useState(0);
  const [viewportHeight, setViewportHeight] = useState(0);
  const viewport = useRef<HTMLDivElement>(null);

  const rows = useMemo(() => {
    if (sort === undefined) {
      return squad.rows;
    }
    const { column, ascending } = sort;
    const direction = ascending ? 1 : -1;
    return [...squad.rows].sort((a, b) => {
      const left = column.value(a);
      const right = column.value(b);
      return (left < right ? -1 : left > right ? 1 : 0) * direction;
    });
  }, [squad.rows, sort]);

  useEffect(() => {
    const element = viewport.current;
    if (element === null) {
      return undefined;
    }
    const observer = new ResizeObserver(() =>
      setViewportHeight(element.clientHeight),
    );
    observer.observe(element);
    return () => observer.disconnect();
  }, []);

  const sortBy = (column: Column) => {
    setSort((current) => ({
      column,
      ascending: current?.column.id === column.id ? !current.ascending : true,
    }));
  };

  const first = Math.max(0, Math.floor(scrollTop / ROW_HEIGHT) - OVERSCAN);
  const last = Math.min(
    rows.length,
    Math.ceil((scrollTop + viewportHeight) / ROW_HEIGHT) + OVERSCAN,
  );
  const totalWidth = columns.reduce((sum, column) => sum + column.width, 0);

  return (
    <div
      ref={viewport}
      className="h-full overflow-auto text-sm"
      onScroll={(event) => setScrollTop(event.currentTarget.scrollTop)}
    >
      <div style={{ width: totalWidth }}>
        <div className="sticky top-0 z-10 flex bg-slate-800 font-semibold text-slate-100">
          {columns.map((column) => (
            <button
              key={column.id}
              type="button"
              className="truncate px-2 py-1 text-left hover:bg-slate-700 focus-visible:outline-2 focus-visible:outline-sky-400"
              style={{ width: column.width }}
              onClick={() => sortBy(column)}
            >
              {column.label}
              {sort?.column.id === column.id
                ? sort.ascending
                  ? " ▲"
                  : " ▼"
                : ""}
            </button>
          ))}
        </div>
        <div className="relative" style={{ height: rows.length * ROW_HEIGHT }}>
          {rows.slice(first, last).map((row, offset) => {
            const index = first + offset;
            return (
              <div
                key={index}
                className={`absolute flex w-full ${index % 2 === 0 ? "bg-slate-900" : "bg-slate-950"}`}
                style={{ top: index * ROW_HEIGHT, height: ROW_HEIGHT }}
              >
                {columns.map((column) => (
                  <div
                    key={column.id}
                    className="truncate px-2 leading-6"
                    style={{ width: column.width }}
                  >
                    {column.text(row)}
                  </div>
                ))}
              </div>
            );
          })}
        </div>
      </div>
    </div>
  );
}
