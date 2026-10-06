import { useQuery } from "@tanstack/react-query";
import { query } from "../ue/bridge";
import { SquadTable } from "../components/SquadTable";

export function SquadScreen() {
  const { data: squad } = useQuery({
    queryKey: ["squad"],
    queryFn: () => query("squad"),
  });

  return squad ? <SquadTable squad={squad} /> : null;
}
