/*
 * The TexviewAppLog class is based on ExampleAppLog from imgui_demo.cpp
 *   Copyright (C) 2014-2025 Omar Cornut and ImGui contributors
 *
 * The rest:
 *   Copyright (C) 2025 Daniel Gibson
 *
 * Released under MIT License, see Licenses.txt
 */

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include "texview.h"
#include <math.h>
#include <time.h>


namespace texview {

void ShowWarningOverlay( const char* text );

// Usage:
//  static ExampleAppLog my_log;
//  my_log.AddLog("Hello %d world\n", 123);
//  my_log.Draw("title");
// heavily based on ExampleAppLog from imgui_demo.cpp
struct TexviewAppLog
{
	ImGuiTextBuffer     Buf;
	ImGuiTextFilter     Filter;
	ImVector<int>       LineOffsets; // Index to lines offset. We maintain this with AddLog() calls.
	bool                AutoScroll;  // Keep scrolling if already at the bottom.

	TexviewAppLog()
	{
		AutoScroll = true;
		Clear();
	}

	void    Clear()
	{
		Buf.clear();
		LineOffsets.clear();
		LineOffsets.push_back(0);
	}

	void    AddLogRaw(const char* start, const char* end = nullptr)
	{
		int old_size = Buf.size();
		Buf.append(start, end);
		for (int new_size = Buf.size(); old_size < new_size; old_size++) {
			if (Buf[old_size] == '\n') {
				LineOffsets.push_back(old_size + 1);
			}
		}
	}

	void    AddLogV(const char* fmt, va_list args)
	{
		int old_size = Buf.size();
		Buf.appendfv(fmt, args);
		for (int new_size = Buf.size(); old_size < new_size; old_size++) {
			if (Buf[old_size] == '\n') {
				LineOffsets.push_back(old_size + 1);
			}
		}
	}

	void    AddLog(const char* fmt, ...) IM_FMTARGS(2)
	{
		va_list args;
		va_start(args, fmt);
		AddLogV(fmt, args);
		va_end(args);
	}

	void    Draw(const char* title, bool* p_open = NULL)
	{
		if (!ImGui::Begin(title, p_open))
		{
			ImGui::End();
			return;
		}

		// Options menu
		if (ImGui::BeginPopup("Options"))
		{
			ImGui::Checkbox("Auto-scroll", &AutoScroll);
			ImGui::EndPopup();
		}

		// Main window
		if (ImGui::Button("Options"))
			ImGui::OpenPopup("Options");
		ImGui::SameLine();
		bool clear = ImGui::Button("Clear");
		ImGui::SameLine();
		if(ImGui::Button("Copy"))
			ImGui::SetClipboardText(Buf.c_str());
		ImGui::SameLine();
		Filter.Draw("Filter", -100.0f);

		ImGui::Separator();

		// DG: no horizontal scrolling
		if (ImGui::BeginChild("scrolling", ImVec2(0, 0), ImGuiChildFlags_None, 0)) // ImGuiWindowFlags_HorizontalScrollbar
		{
			if (clear)
				Clear();

			// DG: wrap log lines
			float wrapWidth = ImGui::GetWindowWidth() - ImGui::GetStyle().ScrollbarSize;
			ImGui::PushTextWrapPos(wrapWidth);

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
			const char* buf = Buf.begin();
			const char* buf_end = Buf.end();
			if (Filter.IsActive())
			{
				// In this example we don't use the clipper when Filter is enabled.
				// This is because we don't have random access to the result of our filter.
				// A real application processing logs with ten of thousands of entries may want to store the result of
				// search/filter.. especially if the filtering function is not trivial (e.g. reg-exp).
				for (int line_no = 0; line_no < LineOffsets.Size; line_no++)
				{
					const char* line_start = buf + LineOffsets[line_no];
					const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
					if (Filter.PassFilter(line_start, line_end))
						ImGui::TextUnformatted(line_start, line_end);
				}
			}
			else
			{
				// The simplest and easy way to display the entire buffer:
				//   ImGui::TextUnformatted(buf_begin, buf_end);
				// And it'll just work. TextUnformatted() has specialization for large blob of text and will fast-forward
				// to skip non-visible lines. Here we instead demonstrate using the clipper to only process lines that are
				// within the visible area.
				// If you have tens of thousands of items and their processing cost is non-negligible, coarse clipping them
				// on your side is recommended. Using ImGuiListClipper requires
				// - A) random access into your data
				// - B) items all being the  same height,
				// both of which we can handle since we have an array pointing to the beginning of each line of text.
				// When using the filter (in the block of code above) we don't have random access into the data to display
				// anymore, which is why we don't use the clipper. Storing or skimming through the search result would make
				// it possible (and would be recommended if you want to search through tens of thousands of entries).
#if 0 // DG: clipper doesn't like line wrapping
				ImGuiListClipper clipper;
				clipper.Begin(LineOffsets.Size);
				while (clipper.Step())
				{
					for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
					{
						const char* line_start = buf + LineOffsets[line_no];
						const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
						ImGui::TextUnformatted(line_start, line_end);
					}
				}
				clipper.End();
#else
				ImGui::TextUnformatted(buf, buf_end);
#endif
			}
			ImGui::PopStyleVar();

			// Keep up at the bottom of the scroll region if we were already at the bottom at the beginning of the frame.
			// Using a scrollbar or mouse-wheel will take away from the bottom edge.
			if (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
				ImGui::SetScrollHereY(1.0f);
		}
		ImGui::EndChild();
		ImGui::End();
	}
};

void StringAppendFormattedV(std::string& str, const char* fmt, va_list args)
{
	va_list args2;
	va_copy(args2, args);
	char buf[2048];

	int len = vsnprintf(buf, sizeof(buf), fmt, args);

	if(len < (int)sizeof(buf)) {
		str += buf;
	} else { // needs more than 2047 chars
		size_t oldSize = str.size();
		str.resize(oldSize + len);
		// write directly into string - knowing the required size,
		// it can be resized accordingly first
		vsnprintf(&str.front() + oldSize, len + 1, fmt, args2);
	}
	va_end(args2);
}

void StringAppendFormatted(std::string& str, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	StringAppendFormattedV(str, fmt, ap);
	va_end(ap);
}

static TexviewAppLog log;
static bool showLogWindow = false;
static bool imguiInitialized = false;

void LogImGuiInit() {
	imguiInitialized = true;
}

void LogWindowShow() {
	showLogWindow = true;
}

void LogWindowHide() {
	showLogWindow = false;
}

bool LogWindowIsShown() {
	return showLogWindow;
}

static void LogImpl(const char* level, const char* fmt, va_list args)
{
	time_t nowT = time(nullptr);
	struct tm now = {};
#ifdef _WIN32
	localtime_s(&now, &nowT);
#else
	localtime_r(&nowT, &now);
#endif
	char timestamp[10] = {};
	strftime(timestamp, sizeof(timestamp), "%H:%M:%S ", &now);
	std::string logLine = timestamp;
	StringAppendFormatted(logLine, "[%s] ", level);
	size_t msgStartOffset = logLine.size();
	StringAppendFormattedV(logLine, fmt, args);

	log.AddLogRaw(logLine.data(), logLine.data() + logLine.length());

	// also log to stderr
	fprintf(stderr, "%s", logLine.c_str());

	// TODO: log to file?

	// if it's a warning or error (level not "Info")
	// show message in warning overlay
	if(imguiInitialized && level[0] != 'I') {
		ShowWarningOverlay(logLine.c_str() + msgStartOffset); // don't show timestamp or [Error]
	}
}

// this one doesn't prepend timestamp and [Error] or whatever
// (useful to log multiple lines)
void LogPrint(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	log.AddLogV(fmt, args);
	va_end(args);
	va_start(args, fmt);
	fprintf(stderr, fmt, args);
	va_end(args);
}

void LogError(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	LogImpl("Error", fmt, args);
	va_end(args);
}

void LogWarn(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	LogImpl("Warn", fmt, args);
	va_end(args);
}

void LogInfo(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	LogImpl("Info", fmt, args);
	va_end(args);
}


static std::string warningOverlayText = "Bla blub whatever!";
static double warningOverlayStartTime = -100.0;
static ImVec2 warningOverlayStartPos;
static int warningOverlayRequested = 0;

static void UpdateWarningOverlay()
{
	if(warningOverlayRequested) {
		// can't initialize these things in ShowWarningOverlay(), because that
		// may be called in the same frame the filepicker was closed, so
		// 1. ImGui's reported mouse position is probably wrong/outdated
		// 2. while the filepicker is open, texview (and thus ImGui) isn't updated
		//    so ImGui::GetTime() (which is a cached value) is outdated at least by seconds
		warningOverlayRequested--;
		warningOverlayStartTime = ImGui::GetTime();
		warningOverlayStartPos = ImGui::GetMousePos();
	} else if(warningOverlayStartTime < 0.0) {
		return;
	}
	//bool hadKeyDownEvent = false; // TODO get this somehow, and also make sure it's not the event that caused this warning? or just support Esc?
	bool hadKeyDownEvent = ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Left);

	if ( hadKeyDownEvent || ImGui::GetTime() - warningOverlayStartTime > 4.0f ) {
		warningOverlayStartTime = -100.0f;
		return;
	}

	// also hide if a key was pressed or maybe even if the mouse was moved (too much)
	//ImVec2 mdv = ImGui::GetMousePos() - warningOverlayStartPos; // Mouse Delta Vector
	const float fontSize = ImGui::GetFontSize();
#if 0

	//float mouseDelta = sqrtf( mdv.x * mdv.x + mdv.y * mdv.y );
	if ( mouseDelta > fontSize * 4.0f || hadKeyDownEvent ) {
		//printf("# mousedelta: %f, keydown: %d\n", mouseDelta, (int)hadKeyDownEvent);
		warningOverlayStartTime = -100.0f;
		return;
	}
#endif

	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4(1.0f, 0.4f, 0.4f, 0.6f) );
	float padSize = fontSize * 2.0f;
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2(padSize, padSize) );

	int winFlags = ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
	ImGui::Begin("WarningOverlay", NULL, winFlags);

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 points[] = {
		{0, 40}, {40, 40}, {20, 0}, // triangle
		{20, 12}, {20, 28}, // line
		{20, 33} // dot
	};

	float iconScale = 1.0f; // TODO: global scale also used for fontsize

	ImVec2 offset = ImGui::GetWindowPos() + ImVec2(fontSize, fontSize);
	for ( ImVec2& v : points ) {
		v.x = roundf( v.x * iconScale );
		v.y = roundf( v.y * iconScale );
		v += offset;
	}

	ImU32 color = ImGui::GetColorU32( ImVec4(0.1f, 0.1f, 0.1f, 1.0f) );

	drawList->AddTriangle( points[0], points[1], points[2], color, roundf( iconScale * 4.0f ) );

	drawList->AddPolyline( points+3, 2, color, 0, roundf( iconScale * 3.0f ) );

	float dotRadius = 2.0f * iconScale;
	drawList->AddEllipseFilled( points[5], ImVec2(dotRadius, dotRadius), color, 0, 6 );

	ImGui::Indent( 40.0f * iconScale );
	ImGui::TextUnformatted( warningOverlayText.c_str() );

	ImGui::End();

	ImGui::PopStyleVar(); // WindowPadding
	ImGui::PopStyleColor(); // WindowBg
}

void ShowWarningOverlay( const char* text )
{
	//printf("XX ShowWarningOverlay('%s')\n", text);
	warningOverlayText = text;
	warningOverlayRequested = 10;
	//warningOverlayStartTime = ImGui::GetTime();
	// FIXME: probably mousepos is bullshit if we're in the filepicker
	//warningOverlayStartPos = ImGui::GetMousePos();
}

void DrawLogWindow() {
	UpdateWarningOverlay();
	if(showLogWindow) {
#if 01 // just for testing..
		ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
		ImGui::Begin("Log Messages", &showLogWindow);

		if (ImGui::SmallButton("[Debug] Add 5 entries"))
		{
			static int counter = 0;
			const char* categories[3] = { "info", "warn", "error" };
			const char* words[] = { "Bumfuzzled", "Cattywampus", "Snickersnee", "Abibliophobia", "Absquatulate", "Nincompoop", "Pauciloquent" };
			for (int n = 0; n < 5; n++)
			{
				const char* category = categories[counter % IM_ARRAYSIZE(categories)];
				const char* word = words[counter % IM_ARRAYSIZE(words)];
				log.AddLog("[%05d] [%s] Hello, current time is %.1f, here's a word: '%s'\n",
						ImGui::GetFrameCount(), category, ImGui::GetTime(), word);
				counter++;
			}
		}
		ImGui::End();
#endif

		log.Draw("Log Messages", &showLogWindow);
	}
}



} // namespace texview
