# Projekt – Urząd miasta

W budynku urzędu miasta znajdują się następujące wydziały/urzędy:

- Urząd Stanu Cywilnego - SC
- Wydział Ewidencji Pojazdów i Kierowców - KM
- Wydział Mieszkalnictwa - ML
- Wydział Podatków i Opłat - PD
- Wydział Spraw Administracyjnych - SA

W Wydziale Spraw Administracyjnych pracuje dwóch urzędników, w pozostałych wydziałach po jednym urzędniku. Każdy z urzędników w danym dniu może przyjąć określoną liczbę osób: urzędnicy SA każdy X1 osób, urzędnik SC X2 osób, urzędnik KM X3 osób, urzędnik ML X4 osób, urzędnik PD X5 osób. Urzędnicy SA przyjmują ok. 60% wszystkich petentów, pozostali urzędnicy każdy po ok. 10% wszystkich petentów (wartość losowa). Do obu urzędników SA jest jedna wspólna kolejka petentów, do pozostałych urzędników kolejki są indywidualne.

Urzędnicy SA część petentów (ok. 40% z X1) kierują do innych urzędników (SC, KM, ML, PD) – bilet do danego urzędnika jest pobierany przez urzędnika SA i „wręczany” danemu petentowi, jeżeli dany urzędnik (SC, KM, ML, PD) nie ma już wolnych terminów dane petenta (id – skierowanie do .. - wystawił) powinny zostać zapisane w raporcie dziennym.

Urzędnicy (SC, KM, ML, PD) dodatkowo mogą skierować petenta do kasy (ok. 10%) celem uiszczenia opłaty, po dokonaniu której petent wraca do danego urzędnika i wchodzi do gabinetu bez kolejki (po uprzednim wyjściu petenta). Jeśli w chwili zamknięcia urzędu w kolejce do urzędnika czekali petenci to te osoby zostaną przyjęte w tym dniu ale nie mogą zostać skierowane do innych urzędników lub do kasy.

Zasady działania urzędu ustalone przez Dyrektora są następujące:

- urząd jest czynny w godzinach od Tp do Tk
- W budynku w danej chwili może się znajdować co najwyżej N petentów (pozostali, jeżeli są czekają przed wejściem)
- Dzieci w wieku poniżej 18 lat do urzędu przychodzą pod opieką osoby dorosłej
- Każdy petent przed wizytą u urzędnika musi pobrać bilet z systemu kolejkowego
- W urzędzie są 3 automaty biletowe, zawsze działa min. 1 stanowisko
- Jeżeli w kolejce po bilet stoi więcej niż K petentów (K>=N/3) uruchamia się drugi automat biletowy. Drugi automat zamyka się jeżeli liczba petentów w kolejce jest mniejsza niż N/3. Jeżeli w kolejkach po bilet stoi więcej niż 2K petentów (K>=N/3) uruchamia się trzeci automat biletowy. Trzeci automat zamyka się jeżeli liczba petentów w kolejkach jest mniejsza niż 2/3*N
- Osoby uprawnione VIP do gabinetu wchodzą bez kolejki
- Jeżeli zostaną wyczerpane limity przyjęć do danego urzędnika, petenci ci nie są przyjmowani (nie mogą pobrać biletu po wyczerpaniu limitu);
- Jeżeli zostaną wyczerpane limity przyjęć w danym dniu do wszystkich urzędników, petenci
nie są wpuszczani do budynku;
- Jeśli w chwili zamknięcia urzędu w kolejce do urzędnika czekali petenci to te osoby nie zostaną przyjęte w tym dniu przez urzędnika. Dane tych petentów (id - sprawa do …. – nr biletu) powinny zostać zapisane w raporcie dziennym (procesy symulujące nieprzyjęte osoby działają jeszcze 2 minuty po zakończeniu symulacji wypisując komunikat: „PID - jestem sfrustrowany”).

Na polecenie Dyrektora (sygnał 1) dany urzędnik obsługuje bieżącego petenta i kończy pracę przed zamknięciem urzędu. Dane petentów (id - skierowanie do … - wystawił), którzy nie zostali przyjęci powinny zostać zapisane w raporcie dziennym.

Na polecenie Dyrektora (sygnał 2) wszyscy petenci natychmiast opuszczają budynek. Napisz procedury Dyrektor, Rejestracja(biletomat), Urzędnik i Petent symulujące działanie urzędu miasta.

Raport z przebiegu symulacji zapisać w pliku (plikach) tekstowym.

## Link

[Github](https://github.com/Lisixo/projekt-urzad-miasta)
