#pragma once

// Frontend-neutral figure automation operations shared by the legacy and Qt
// dispatchers. Each operation writes a complete JSON response to the request.

namespace spectra
{

class FigureRegistry;
struct AutomationRequest;

bool automation_add_series(AutomationRequest& request, FigureRegistry& registry);
bool automation_get_figure_info(AutomationRequest& request, FigureRegistry& registry);

}   // namespace spectra
