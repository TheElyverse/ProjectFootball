# Elyverse: Football — Game Design Document

Tagline: Living Football

*Eine moderne, systemische Fußballmanagement-Simulation*

Status: Konzeptgrundlage / Pre-Production

**Design-Leitmotiv**

*„Maximale Simulationstiefe bei minimaler Verwaltungsarbeit.“*

## Inhalt

| **1**  | Vision und Positionierung                |
| ------ | ---------------------------------------- |
| **2**  | Designprinzipien                         |
| **3**  | Core Gameplay Loop                       |
| **4**  | Management & Delegation                  |
| **5**  | Living Football World                    |
| **6**  | Spieler- und Entwicklungsmodell          |
| **7**  | Taktiksystem                             |
| **8**  | Match Simulation                         |
| **9**  | Training & Coaching                      |
| **10** | Recruitment, Transfers & Verträge        |
| **11** | Club AI & Mitarbeiter                    |
| **12** | UX / Desktop-first Interface             |
| **13** | Karriere, Saison und Langzeitspiel       |
| **14** | Analytics & Erklärbarkeit                |
| **15** | Progression, Schwierigkeit und Spielmodi |
| **16** | Scope des Vertical Slice                 |
| **17** | Nicht-Ziele und Anti-Features            |
| **18** | Erfolgskriterien und offene Designfragen |

## 1. Vision und Positionierung

Project Football soll nicht „Football Manager mit anderer Oberfläche“ sein, sondern eine glaubwürdige Fußballwelt, in der Entscheidungen systemische Konsequenzen haben.

> **Kernversprechen**
>
> Der Spieler trifft relevante Entscheidungen. Routinearbeit kann delegiert werden. Dieselben Systeme gelten für Spieler und KI. Die Welt entwickelt sich auch ohne den Spieler weiter.

### 1.1 Produktvision

Project Football ist eine tiefe Fußballmanagement-Simulation mit drei gleichwertigen Säulen: Living Football World, Deep Football Simulation und Zero-Busywork Management. Die Simulation soll nicht primär durch mehr Menüpunkte, sondern durch stärker miteinander gekoppelte Systeme tiefer werden.

### 1.2 Differenzierung

| **Säule**                | **Versprechen**                                           | **Konkrete Konsequenz**                                                                         |
| ------------------------ | --------------------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| Living Football World    | Vereine, Ligen, Personen und Märkte verändern sich.       | Langzeit-Saves entwickeln eine eigene Geschichte statt im Ausgangszustand einzufrieren.         |
| Deep Football Simulation | Spieler nehmen wahr, entscheiden und handeln individuell. | Taktik wird auf dem Platz sichtbar; Verhalten entsteht aus Fähigkeiten, Kontext und Prinzipien. |
| Zero-Busywork Management | Keine Pflichtklicks ohne Entscheidung.                    | Delegation, Policies, Action Queue und ereignisgetriebene Kommunikation.                        |

### 1.3 Zielplattform und Zielgruppe

- Primärplattform: PC. Desktop-first statt Mobile-first.
- Zielgruppe: Spieler, die tiefes Fußballmanagement wünschen, aber repetitive Mikroverwaltung ablehnen.
- Sekundäre Zielgruppe: Taktik- und Datenfans, die verstehen möchten, warum ein Spiel funktioniert oder scheitert.
- Singleplayer-first. Multiplayer bleibt architektonisch möglich, ist aber kein Pre-Production-Ziel.

## 2. Designprinzipien

| **Prinzip**                    | **Bedeutung**                                                                                           |
| ------------------------------ | ------------------------------------------------------------------------------------------------------- |
| No decision → no click         | Wiederkehrende Aktionen ohne interessante Wahl werden automatisiert, zusammengefasst oder delegiert.    |
| Same systems for player and AI | Sportdirektoren, Trainer und KI-Vereine verwenden dieselben Daten, Budgets und Entscheidungsmodelle.    |
| Simulation before presentation | Das Ergebnis entsteht im Core; Unreal visualisiert und inszeniert es.                                   |
| Explainable outcomes           | Die Engine soll Ursachen liefern können: Wahrnehmung, Entscheidung, Ausführung, Taktik und Kontext.     |
| Uncertainty is gameplay        | Scouting und Coaching zeigen Schätzungen und Konfidenzen statt allwissender Zahlen.                     |
| Emergence over scripting       | Pressing, Kombinationen, Momentum und Karriereverläufe sollen möglichst aus Regeln entstehen.           |
| Manual or delegated            | Jede relevante Managementaufgabe kann selbst ausgeführt oder an Personal mit Policies delegiert werden. |
| PC productivity UX             | Suche, Tastatursteuerung, Tabs, History, Bulk Actions und konfigurierbare Tabellen sind Kernfeatures.   |

## 3. Core Gameplay Loop

> ANALYZE → DECIDE → DELEGATE / EXECUTE → WATCH RESULTS → ADAPT

Der Spieler soll einen wiederkehrenden Rhythmus erleben, in dem Informationen zu Entscheidungen führen und Entscheidungen beobachtbare Konsequenzen erzeugen. Das Spiel darf nicht zu einem „Weiter“-Simulator werden.

### 3.1 Tages-/Wochenrhythmus

1. Action Queue prüfen: nur Themen mit Entscheidungsbedarf.
2. Team- und Gegneranalyse betrachten.
3. Taktik, Training, Kader oder Transfers anpassen – selbst oder delegiert.
4. Zeit bis zum nächsten relevanten Ereignis fortschreiten lassen.
5. Match beobachten und situativ reagieren.
6. Nachbereitung: Ursachen, Entwicklung und neue Handlungsbedarfe verstehen.

### 3.2 Advance to next relevant event

Zeitfortschritt orientiert sich an relevanten Ereignissen. Der Spieler kann granular bis zu einem Termin springen, ohne jeden Kalendertag manuell zu bestätigen. Kritische Eskalationen unterbrechen automatisch.

## 4. Management & Delegation

> **Grundsatz**
>
> Delegation ist kein Casual Mode. Delegierte Mitarbeiter führen denselben Workflow aus, den der Spieler manuell übernehmen kann.

### 4.1 Responsibility System

| **Bereich** | **Beispielaufgaben**                           | **Typische Verantwortliche**             |
| ----------- | ---------------------------------------------- | ---------------------------------------- |
| First Team  | Aufstellung, Matchplan, Matchvorbereitung      | Manager / Assistant Manager              |
| Training    | Wochenplan, Individualtraining, Reha           | Manager / Coaching Staff / Medical Staff |
| Recruitment | Bedarf, Suche, Scouting, Shortlist             | Manager / Sporting Director / Head Scout |
| Transfers   | Angebote, Verhandlungen, Verkäufe, Leihen      | Manager / Sporting Director              |
| Contracts   | Verlängerungen, Rollen, Boni                   | Manager / Sporting Director              |
| Youth       | Entwicklungspläne, Beförderungen, Leihen       | Manager / Head of Youth Development      |
| Media       | Relevante Presse- und Kommunikationsereignisse | Manager / Press Officer                  |
| Staff       | Suche, Rollen, Verträge                        | Manager / Sporting Director              |

### 4.2 Policies und Eskalationen

Delegation erfolgt über Ziele, Grenzen und Eskalationsregeln. Beispiel Recruitment Policy: rechter Innenverteidiger, 18–24 Jahre, aufbaustark, maximal 25 Mio. Ablöse; Eskalation bei mehr als 18 Mio., ungewöhnlicher Spielerrolle oder ähnlich bewerteten Kandidaten.

- Policies sind persistent und können pro Verantwortungsbereich gespeichert werden.
- Mitarbeiter besitzen eigene Präferenzen und Kompetenzen; gute Delegation hängt vom passenden Personal ab.
- Der Spieler kann jederzeit „Take Control“ oder „Delegate“ wählen, ohne Systemwechsel.
- Delegierte Aktionen müssen nachvollziehbar sein: Mitarbeiter erläutern Empfehlungen und Trade-offs.

## 5. Living Football World

Die Welt ist kein statischer Hintergrund. Vereine, Mitarbeiter, Spieler, Ligen und Märkte verfolgen Ziele und reagieren auf Ergebnisse. Langfristige Saves sollen sich deutlich vom Startjahr entfernen können, ohne beliebig zu wirken.

### 5.1 Club Identity

| **Dimension**       | **Beispiele**                           | **Dynamik**                                                          |
| ------------------- | --------------------------------------- | -------------------------------------------------------------------- |
| Sportstrategie      | Jugend, Stars, Trading, Kontinuität     | Vorstand und Sportdirektor können Prioritäten verschieben.           |
| Finanzrisiko        | konservativ bis aggressiv               | Erfolg, Eigentümer, Schulden und Wettbewerb verändern Risikoappetit. |
| Taktische Identität | Ballbesitz, Pressing, Direktspiel       | Trainerhistorie, Akademie und Erfolgsmodelle prägen den Club.        |
| Recruiting          | Alter, Regionen, Profile                | Netzwerk, Marktchancen und Strategiewechsel verändern Fokus.         |
| Akademie            | Investition, Spielidee, Durchlässigkeit | Budget und Clubphilosophie beeinflussen Talentpipeline.              |

### 5.2 Dynamische Fußballökonomie

- TV- und Sponsoringattraktivität reagieren langfristig auf sportlichen Erfolg und internationale Aufmerksamkeit.
- Ligen können an Prestige, Gehaltsniveau, Talentbindung und Transferattraktivität gewinnen oder verlieren.
- Build-a-Nation-Saves sollen systemisch unterstützt werden.
- Transfermärkte reagieren auf Angebotsknappheit, Nachfrage, Vertragslaufzeiten, Agenten und finanzielle Lage.

### 5.3 Personen und Beziehungen

Trainer, Direktoren, Spieler und Agenten entwickeln Beziehungen, Reputation und Präferenzen. Personalwechsel können die Identität eines Vereins schrittweise verändern. Rivalitäten und Karrieregeschichten sollen aus Ereignissen entstehen statt ausschließlich vorgegeben zu sein.

## 6. Spieler- und Entwicklungsmodell

### 6.1 Drei Ebenen

> Underlying Capability → Observed Behaviour → Scout / Coach Assessment

Die Simulation besitzt interne Fähigkeiten. Verhalten entsteht daraus im konkreten Kontext. Scouts und Trainer beobachten dieses Verhalten und bilden unsichere Bewertungen. Der Manager kennt deshalb nicht automatisch die „Wahrheit“.

### 6.2 Fähigkeitsdomänen

| **Domäne**   | **Beispiele**                                                                                      |
| ------------ | -------------------------------------------------------------------------------------------------- |
| Körper       | Beschleunigung, Geschwindigkeit, Kraft, Ausdauer, Agilität, Körperbau, Verletzungsrisiko           |
| Technik      | Ballannahme, Kurz-/Langpass, Schuss, Flanke, Dribbling, Kopfball, schwacher Fuß                    |
| Kognition    | Scanning, Wahrnehmung, Antizipation, Entscheidungsqualität, Reaktionszeit, räumliches Verständnis  |
| Psychologie  | Professionalität, Ehrgeiz, Stressresistenz, Kreativität, Risikoneigung, Disziplin, Selbstvertrauen |
| Taktik       | Prinzipverständnis, Rollenwissen, Formation, Mitspieler-Familiarität                               |
| Gewohnheiten | bevorzugte Laufwege, Passmuster, Körperorientierung, typische Entscheidungen                       |

### 6.3 Entwicklung

Es gibt keine einzelne sichtbare Potential-Zahl. Entwicklung ist eine Kurve mit Unsicherheit und Kontextabhängigkeit. Early Bloomers, Late Bloomers, Plateaus, Durchbrüche und Regression sind möglich.

> Development = Genetics × Age × Training × Coaching × PlayingTime × Challenge × Mentality × Health × Environment

- Optimal Challenge: Entwicklung ist am stärksten bei angemessen anspruchsvollem Niveau.
- Verletzungen können Entwicklung verzögern oder Profile verändern.
- Mentale Reifung und taktische Entwicklung können länger anhalten als körperliche Entwicklung.
- Leihen werden zu einer echten Entwicklungsentscheidung statt zu einem simplen Spielzeitbonus.

### 6.4 Prozedurale Spielergenerierung

Spieler für die Kader zum Karrierestart und spätere Nachwuchsgenerationen werden prozedural erzeugt. Jeder Spieler besitzt eine eigene Identität, ein individuelles Fähigkeitsprofil, eine Persönlichkeit und Entwicklungsmöglichkeiten. Alter sowie nationale und clubbezogene Rahmenbedingungen prägen plausible, vielfältige Profile.

Dadurch bieten unterschiedliche Karrieren neue Talente und Kaderkonstellationen, die der Manager durch Scouting und Beobachtung entdecken und einschätzen muss. Generierte Spieler unterliegen demselben Beobachtungs- und Entwicklungsmodell wie alle anderen Spieler: Ihre Fähigkeiten sind nicht unmittelbar vollständig bekannt, und ihr Werdegang hängt von Training, Spielzeit, Gesundheit und Umfeld ab.

## 7. Taktiksystem

Taktik wird als Hierarchie aus Team Principles, Phase Instructions und Player Responsibilities modelliert. Klassische Rollen können als Presets angeboten werden, sind aber nicht die eigentliche Datenstruktur.

### 7.1 Ebenen

| **Ebene**               | **Beispiele**                                                                     |
| ----------------------- | --------------------------------------------------------------------------------- |
| Team Principles         | Build from back, central overloads, counterpress, compact block                   |
| Phases                  | Build-Up, Progression, Final Third, Defensive Block, Pressing, Transition         |
| Player Responsibilities | Breite geben, Halfspace besetzen, absichern, unterlaufen, Pressinglinie schließen |

### 7.2 Formation als Referenz

4-3-3 oder 4-4-2 definieren primär Ausgangs- und Defensivstrukturen. In Ballbesitz entstehen dynamische Formen aus Verantwortlichkeiten, Raum und Spielsituation. Spieler besitzen bevorzugte Regionen statt starrer Zielpunkte.

### 7.3 Taktisches Verständnis

- FormationKnowledge – Verständnis der Grundstruktur.
- RoleKnowledge – Verständnis der eigenen Verantwortlichkeiten.
- TeamPrinciples – Verinnerlichung der Spielidee.
- TeammateFamiliarity – Timing und automatisierte Beziehungen mit Mitspielern.

## 8. Match Simulation

> **Architekturprinzip**
>
> Die Match Simulation entscheidet, was passiert. Unreal Engine entscheidet, wie es aussieht.

### 8.1 Kontinuierliche Welt, ereignisorientierte Entscheidungen

Spieler bewegen sich kontinuierlich auf einem metrischen Spielfeld. Wahrnehmung, Entscheidung, taktische Bewertung und Physik laufen mit unterschiedlichen Frequenzen. Der Ballführer entscheidet häufiger als ein weit entfernter Spieler.

| **Subsystem**       | **Richtwert**   |
| ------------------- | --------------- |
| Movement / Physics  | 20–30 Hz        |
| Ball Simulation     | 30–60 Hz        |
| Perception          | 5–10 Hz         |
| Decision Making     | 2–10 Hz adaptiv |
| Tactical Evaluation | 1–5 Hz          |
| Statistics          | event-basiert   |

### 8.2 Raum und Pitch Control

Neben kontinuierlichen Koordinaten existieren semantische Räume (Flügel, Halbräume, Zwischenlinienräume, Restverteidigung, Pressingfallen). Eine dynamische Occupancy/Pitch-Control-Map schätzt, welches Team welchen Raum wie schnell kontrollieren kann.

### 8.3 Wahrnehmung und Scanning

Die Engine kennt die Realität, ein Spieler nur sein persönliches World Model. Sichtfeld, Kopf-/Körperorientierung, Entfernung, Druck, Aufmerksamkeit und Scanning bestimmen, welche Optionen überhaupt erkannt werden. Gesehene Spieler werden mit Zeitstempel und Konfidenz gespeichert und extrapoliert.

### 8.4 Decision Pipeline

> Perceive → Generate candidate actions → Estimate utility → Select probabilistically → Execute technically → Observe consequences

Entscheidungsqualität und technische Ausführung werden getrennt. Ein Spieler kann die richtige Entscheidung treffen und den Pass technisch verfehlen – oder eine schlechte Option perfekt ausführen.

### 8.5 Ballbesitzaktionen

- Pass zu Spieler oder in Raum
- Carry
- Dribble
- Shoot
- Cross
- Hold / Turn
- Clear / Safety action

### 8.6 Off-ball actions

- Support
- Move into space
- Make run
- Create width
- Occupy halfspace
- Overlap / Underlap
- Attack box
- Cover
- Press
- Mark
- Block lane
- Track runner

### 8.7 Pressing und Defensive

Pressing basiert auf Triggern wie schlechter Ballannahme, Rückpass, Blick zum eigenen Tor, isoliertem Empfänger oder langsamem Pass. Pressing ist nur erfolgreich, wenn Mitspieler Deckung, Linienhöhe und Passwege koordinieren. Unkoordiniertes Pressing soll sichtbar scheitern.

### 8.8 Duelle, Dribbling, Schüsse, Torwart

Duelle werden als kurze Sequenzen aus Position, Geschwindigkeit, Winkel, Balance, Ballkontrolle und Entscheidungen modelliert – nicht als einzelner Erfolgswurf. Schüsse trennen Zielwahl von Ausführung. Torhüter besitzen ein eigenes Positions-, Antizipations-, Sweeper- und Distributionsmodell.

### 8.9 Müdigkeit und Mentalität

Müdigkeit wirkt auf Sprintkapazität, Beschleunigung, technische Präzision, Scan-Frequenz und Entscheidungszeit. Mentale Zustände wie Confidence, Composure, Frustration und Focus verändern Verhalten subtil; es gibt keine pauschalen Arcade-Buffs und keinen versteckten Momentum-Regler.

## 9. Training & Coaching

Training verändert reale Fähigkeiten, Gewohnheiten und taktische Familiarität. Es gibt keinen abstrakten „Match Bonus“, wenn eine Übung keinen plausiblen Effekt auf das Verhalten hat.

| **Trainingsziel**             | **Mögliche Effekte**                                                      |
| ----------------------------- | ------------------------------------------------------------------------- |
| Build-up gegen hohes Pressing | Scan-Frequenz, Support-Positionierung, Passvertrauen, Pattern Familiarity |
| Defensive Kompaktheit         | Abstände, Verschieben, Cover-Verhalten, Trigger-Erkennung                 |
| Final Third                   | Laufweg-Timing, Box Occupation, Kombinationen, Abschlussentscheidungen    |
| Individualentwicklung         | Technik, körperliche Parameter, Gewohnheiten, schwacher Fuß               |

Trainer besitzen nicht nur eine Gesamtstärke, sondern Fähigkeiten wie TechnicalTeaching, TacticalTeaching, Communication, Motivation, TalentRecognition und Adaptability sowie eine Coaching Philosophy.

## 10. Recruitment, Transfers & Verträge

### 10.1 Recruitment Strategy

- Kaderanalyse erzeugt Bedarf statt isolierter Transferwünsche.
- Suche kann manuell oder über Policies delegiert werden.
- Scouting liefert Bewertungen mit Unsicherheit und begrenztem Wissen.
- Kandidaten werden nach taktischem Fit, Entwicklungsprofil, Verfügbarkeit, Risiko und Kosten verglichen.
- Agenten, Konkurrenz, Vertragslaufzeit und Clubstatus beeinflussen Verhandlungen.

### 10.2 Transfermarkt als Markt

Preise sollen nicht ausschließlich aus einer statischen Reputation-/Ability-Formel entstehen. Nachfrage, Angebotsknappheit, Vertragslage, Finanzdruck, Strategie, Agenten und Kaderbedarf erzeugen Marktpreise. KI-Klubs müssen dieselben Restriktionen beachten.

## 11. Club AI & Mitarbeiter

Jeder KI-Verein führt fortlaufend Squad Planning durch. Ausgangspunkte sind Kaderstärke, Tiefe, Altersstruktur, Verträge, Finanzen, taktischer Fit, Nachwuchs und Ambition.

> Squad Analysis → Priorities → Search → Scout → Compare → Negotiate → Integrate / Sell / Loan

- KI kauft nach Bedarf und Strategie, nicht nach isoliertem Gesamtwert.
- Mitarbeiter handeln gemäß eigener Kompetenz, Philosophie und Informationslage.
- Trainer-AI analysiert Matches und kann auf Überladungen, Aufbauprobleme, Pressing oder Mismatches reagieren.
- Vorstände definieren Ziele und akzeptierbare Risiken statt nur Endplatzierungen.

## 12. UX / Desktop-first Interface

> **UX-Ziel**
>
> Eine Manageroberfläche soll sich eher wie ein produktives Analysewerkzeug als wie eine Folge mobiler Karten und Modals anfühlen.

| **Feature**              | **Zweck**                                                        |
| ------------------------ | ---------------------------------------------------------------- |
| Action Queue             | Nur Entscheidungen und Eskalationen anzeigen.                    |
| Command Palette (Ctrl+K) | Spieler, Teams, Funktionen und Aktionen global finden.           |
| Tabs                     | Mehrere Profile / Analysen parallel offen halten.                |
| History                  | Alt+Left / Alt+Right wie im Browser.                             |
| Bulk Actions             | Mehrfachauswahl für Scouting, Training, Kaderaktionen.           |
| Custom Views             | Tabellen, Spalten, Sortierung und Filter speicherbar.            |
| Keyboard Navigation      | Fast alle häufigen Workflows ohne Maus.                          |
| Context Preservation     | Keine tiefen Modal-Ketten, Rückkehr an denselben Arbeitskontext. |

### 12.1 Presse und Kommunikation

Presse ist ereignisgetrieben. Normale Spiele erfordern keine Pflichtkonferenz. Relevante Ereignisse – Finalspiele, Krisen, Transfers, Rivalitäten – können Kommunikation auslösen. Antworten haben Trade-offs, aber keine versteckte „richtige“ Option.

## 13. Karriere, Saison und Langzeitspiel

- Saisonkalender mit Wettbewerben, Registrierung, Transferfenstern und Periodisierung.
- Karrierehistorie für Spieler, Mitarbeiter, Vereine und Ligen.
- Dynamische Reputation und Attraktivität.
- Nachwuchsgenerationen mit plausiblen Profilen und nationalen / clubbezogenen Entwicklungsbedingungen.
- Welt kann mit unterschiedlichen Simulationsauflösungen laufen; relevante Wettbewerbe detaillierter, entfernte Bereiche approximiert.

### 13.1 Simulations-Level

| **Level** | **Beschreibung**                         | **Einsatz**                    |
| --------- | ---------------------------------------- | ------------------------------ |
| 0         | Statistische Approximation               | Entfernte Ligen / Hintergrund  |
| 1         | Vereinfachte Positions-/Kader-Simulation | Sekundäre Wettbewerbe          |
| 2         | Volle taktische Matchsimulation          | Relevante Spiele               |
| 3         | Volle Simulation + Unreal-Präsentation   | Vom Spieler angesehene Matches |

## 14. Analytics & Erklärbarkeit

Analytics sind direkt aus der Simulation abzuleiten. Da Wahrnehmung, Kandidaten, Utility und Ausführung bekannt sind, kann die Engine Ursachen erklären statt nur Statistiken anzeigen.

> **Beispiel**
>
> „6 Ballverluste des linken Innenverteidigers entstanden unter hohem Druck. In vier Situationen erkannte er den zentralen Sechser zu spät; in zwei Situationen war die Entscheidung gut, die Passausführung jedoch unpräzise.“

- xG, Pass Maps, Heatmaps, Pitch Control, Progressive Actions, PPDA, Pressing Chains.
- Taktische Hinweise durch Assistant Manager mit konkreten Ursachen und Handlungsoptionen.
- Replay Debugger intern; vereinfachte „Why?“-Ansicht optional für Spieler.
- Analyse soll zu Entscheidungen führen, nicht nur Datenmenge erzeugen.

## 15. Progression, Schwierigkeit und Spielmodi

Schwierigkeit sollte möglichst aus Informationslage, Ressourcen, Clubgröße und Qualität der Konkurrenz entstehen – nicht durch versteckte KI-Boni. Optional können Komfortstufen für Informationsmenge, Delegation und Assistenz angeboten werden.

| **Modusidee**   | **Charakter**                                                               |
| --------------- | --------------------------------------------------------------------------- |
| Full Management | Alle Systeme verfügbar; Verantwortungen frei konfigurierbar.                |
| Head Coach      | Transfers/Finanzen primär delegiert; Fokus Taktik, Training und Mannschaft. |
| Director Mode   | Kader- und Clubstrategie im Vordergrund; Matchday stärker delegiert.        |
| Sandbox         | Taktische Experimente und Matchsimulation ohne Karriere.                    |

## 16. Scope des Vertical Slice

Der Vertical Slice soll die riskantesten Kernannahmen beweisen, nicht den gesamten Karriereumfang imitieren.

| **Phase**               | **Scope**                                                           | **Proof**                                                                  |
| ----------------------- | ------------------------------------------------------------------- | -------------------------------------------------------------------------- |
| A – Football Sandbox    | 7v7 → 11v11, 2D/headless, drei Taktikstile                          | Spielidentität entsteht sichtbar und statistisch.                          |
| B – Mini League         | 4 Clubs, Saison, Fitness, Training, Entwicklung, einfache Transfers | Systeme interagieren über Wochen und Monate.                               |
| C – Management Layer    | Staff, Delegation, Recruitment Policies, Action Queue, Club AI      | Manuell und delegiert funktionieren über dieselben Workflows.              |
| D – Unreal Presentation | 3D-Spieltag, Kamera, Animation, UI                                  | Darstellung konsumiert Simulationszustand ohne Gameplay-Logik zu besitzen. |

### 16.1 Erster taktischer Proof

Drei Teams mit identischer Spielerstärke: Possession, Counter, Pressing. Nach tausenden Simulationen müssen die Spielstile statistisch unterscheidbar sein; in einzelnen Matches müssen die taktischen Muster visuell bzw. in Events erkennbar werden.

## 17. Nicht-Ziele und Anti-Features

- Keine verpflichtenden repetitiven Pressekonferenzen.
- Kein wöchentliches Lob-/Moral-Minigame.
- Kein Social Feed voller belangloser Meldungen.
- Kein Privatleben-, Auto- oder Wohnungs-Metaspiel als Kernfeature.
- Keine tägliche Pflicht-Mikroverwaltung von Training oder Meetings.
- Keine KI, die beim Delegieren außerhalb derselben Regeln cheatet.
- Keine versteckte Momentum-Mechanik.
- Keine 3D-Produktion, bevor der headless Fußballkern plausibel funktioniert.

## 18. Erfolgskriterien und offene Designfragen

### 18.1 Erfolgskriterien

- Spielstile sind in Telemetrie und Matchverlauf zuverlässig unterscheidbar.
- Delegation reduziert Klicks, ohne Entscheidungen oder Kontrolle zu entfernen.
- Langzeitwelten verändern sich plausibel statt chaotisch oder statisch.
- Spielerprofile erzeugen unterschiedliche Verhaltensweisen, nicht nur unterschiedliche Erfolgswahrscheinlichkeiten.
- Der Spieler kann zentrale Ergebnisse nachvollziehen und darauf reagieren.
- Ein normaler Ingame-Tag kann ohne bedeutungslose Pflichtinteraktionen übersprungen werden.

### 18.2 Offene Fragen für Pre-Production

- Wie viel Unsicherheit ist im Scouting unterhaltsam, ohne frustrierend zu werden?
- Welche taktischen Freiheiten benötigen Power User, ohne die UI zu überladen?
- Welche Simulationsauflösung ist für zehntausende Weltmatches wirtschaftlich?
- Wie stark dürfen Clubidentitäten driften, ohne ihre historische Glaubwürdigkeit zu verlieren?
- Wie werden echte Lizenzen / Daten später optional integriert, ohne das Kernsystem davon abhängig zu machen?
- Welche Teile der Match-Engine benötigen echte 3D-Physik und welche sollten deterministisch im Core bleiben?
