import { useEffect, useState } from "react";
import { query, type SquadView } from "../bridge";
import { SquadTable } from "../components/SquadTable";

export function SquadScreen() {
  const [squad, setSquad] = useState<SquadView>();

  useEffect(() => {
    void query("squad").then(setSquad);
  }, []);

  return squad ? <SquadTable squad={squad} /> : null;
}
