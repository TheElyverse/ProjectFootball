import { useQuery } from "@tanstack/react-query";
import { query } from "@/ue/bridge";
import { SquadTable } from "./SquadTable";

export function Squad() {
  const { data: squad } = useQuery({
    queryKey: ["squad"],
    queryFn: () => query("squad"),
  });

  return squad ? <SquadTable squad={squad} /> : null;
}
