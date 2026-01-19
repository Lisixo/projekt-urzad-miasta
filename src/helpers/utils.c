#include "consts.h"

const char* faculty_to_str(FacultyType t) {
  switch (t % 10) {
    case FACULTY_SC:
      return "Urzad Stanu Cywilnego";
    case FACULTY_KM:
      return "Wydzial Ewidencji Pojazdow i Kierowcow";
    case FACULTY_ML:
      return "Wydzial Mieszkalnictwa";
    case FACULTY_PD:
      return "Wydzial Podatkow i Oplat";
    case FACULTY_SA:
      return "Wydzial Spraw Administracyjnych";
    case CASHIER_POINT:
      return "Kasa";
    default:
      return "Do jakiegos wydzialu";
  }
}