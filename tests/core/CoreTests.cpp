#include "../../src/core/GameEngine.h"
#include "../../src/core/ScoreService.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace nc;

namespace {

int failures = 0;

void expect(const bool condition, const char* message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

Chart basicChart()
{
    Chart chart;
    chart.id = QStringLiteral("core");
    chart.notes = {
        {1, NoteType::Tap, 0, 1000, 1000, -1, -1},
        {2, NoteType::Hold, 1, 2000, 3000, -1, -1},
        {3, NoteType::Tap, 2, 3500, 3500, -1, 1},
        {4, NoteType::Tap, 3, 3500, 3500, -1, 1},
        {5, NoteType::ChainStep, 0, 4000, 4000, 1, -1},
        {6, NoteType::ChainStep, 1, 4300, 4300, 1, -1},
    };
    return chart;
}

Chart oneNoteChart(const NoteType type = NoteType::Tap)
{
    Chart chart;
    chart.id = QStringLiteral("single");
    chart.notes = {{1, type, 0, 1000, type == NoteType::Hold ? 2000 : 1000, -1, -1}};
    return chart;
}

void testExactWindowsAndOffset()
{
    const JudgeConfig config;
    expect(config.perfectMs == 90 && config.greatMs == 150 && config.goodMs == 220
            && config.rulesVersion == "2", "default rules use the wider version 2 windows");
    const std::vector<qint64> deltas{-221, -220, -151, -150, -91, -90, 0, 90, 91, 150, 151, 220, 221};
    for (const int offset : {-200, -75, 0, 75, 200}) {
        for (const qint64 delta : deltas) {
            GameEngine engine;
            expect(engine.startSession(oneNoteChart(), {}, offset), "window test starts");
            const qint64 inputTime = 1000 + delta - offset;
            const bool hit = engine.handleKeyPress(0, inputTime);
            const qint64 absolute = std::llabs(delta);
            expect(hit == (absolute <= config.goodMs), "exact calibrated window accepts only allowed inputs");
            engine.advanceTo(1500);
            const auto& stats = engine.stats();
            expect(stats.finalizedCount == 1, "window note finalized exactly once");
            if (absolute <= config.perfectMs) {
                expect(stats.perfect == 1, "perfect boundary inclusive");
            } else if (absolute <= config.greatMs) {
                expect(stats.great == 1, "great boundary inclusive");
            } else if (absolute <= config.goodMs) {
                expect(stats.good == 1, "good boundary inclusive");
            } else {
                expect(stats.miss == 1, "out of window misses");
            }
        }

        GameEngine deadline;
        deadline.startSession(oneNoteChart(), {}, offset);
        deadline.advanceTo(1000 + config.goodMs - offset);
        expect(deadline.stats().finalizedCount == 0, "latest valid calibrated input is not expired");
        deadline.advanceTo(1001 + config.goodMs - offset);
        expect(deadline.stats().miss == 1, "head expires one millisecond past calibrated deadline");
    }
}

void testWindowsAndRepeat()
{
    GameEngine engine;
    expect(engine.startSession(basicChart()), "session starts");
    expect(engine.handleKeyPress(0, 1000), "perfect press accepted");
    expect(!engine.handleKeyPress(0, 1000, true), "auto repeat ignored");
    engine.handleKeyRelease(0, 1001);
    expect(engine.stats().perfect == 1 && engine.stats().finalizedCount == 1, "single point once");

    expect(engine.handleKeyPress(1, 2100), "great hold head accepted");
    engine.advanceTo(3000);
    expect(engine.stats().great == 1 && engine.stats().finalizedCount == 2, "hold tail finalizes once");
    engine.handleKeyRelease(1, 3000);
    expect(engine.stats().finalizedCount == 2, "tail release does not duplicate");
}

void testEarlyReleaseAndIndependentChords()
{
    Chart chart = basicChart();
    chart.notes.erase(chart.notes.begin());

    GameEngine engine;
    expect(engine.startSession(chart), "second session starts");
    expect(engine.handleKeyPress(1, 2000), "hold head accepted");
    engine.handleKeyRelease(1, 2500);
    expect(engine.stats().miss == 1, "early release is one miss");

    expect(engine.handleKeyPress(2, 3500), "first chord member accepted");
    engine.handleKeyRelease(2, 3501);
    engine.advanceTo(3721);
    expect(engine.stats().miss == 2, "unpressed chord member misses independently");
    engine.advanceTo(3800);
    expect(engine.stats().miss == 2, "finalized chord members do not repeat");
}

void testScoreAndGrade()
{
    PlayStats stats;
    for (int i = 0; i < 8; ++i) {
        ScoreService::applyJudgement(stats, Judgement::Perfect);
    }
    ScoreService::applyJudgement(stats, Judgement::Great);
    ScoreService::applyJudgement(stats, Judgement::Miss);
    const auto result = ScoreService::buildResult(stats, 10);
    expect(result.complete, "complete stats accepted");
    expect(result.score == 870000, "score uses integer formula");
    expect(std::abs(result.accuracy - 0.87) < 1e-9, "accuracy uses exact earned units");
    expect(result.grade == QStringLiteral("B"), "grade B at 87 percent");

    PlayStats perfect;
    for (int i = 0; i < 10; ++i) {
        ScoreService::applyJudgement(perfect, Judgement::Perfect);
    }
    const auto perfectResult = ScoreService::buildResult(perfect, 10);
    expect(perfectResult.score == 1000000 && perfectResult.grade == QStringLiteral("S"), "perfect is S");
    expect(perfectResult.fullCombo && perfectResult.allPerfect, "perfect also gives FC and AP");

    PlayStats weakCombo;
    for (int i = 0; i < 50; ++i) {
        ScoreService::applyJudgement(weakCombo, Judgement::Perfect);
    }
    ScoreService::applyJudgement(weakCombo, Judgement::Miss);
    for (int i = 0; i < 49; ++i) {
        ScoreService::applyJudgement(weakCombo, Judgement::Perfect);
    }
    const auto comboResult = ScoreService::buildResult(weakCombo, 100);
    expect(comboResult.score == 990000 && comboResult.grade == QStringLiteral("A"),
        "high accuracy needs combo ratio for S");
    expect(!comboResult.fullCombo && !comboResult.allPerfect, "missing a note does not imply FC");

    auto broken = weakCombo;
    ++broken.earnedUnits;
    expect(!ScoreService::isValidStats(broken, 100, true), "statistics reject forged earned units");
    broken = weakCombo;
    broken.maxCombo = -1;
    expect(!ScoreService::isValidStats(broken, 100, true), "statistics reject negative combo");
    broken = weakCombo;
    broken.maxCombo = 1;
    broken.combo = 1;
    expect(!ScoreService::isValidStats(broken, 100, true), "statistics reject impossible success segments");
    expect(!ScoreService::isComplete({}, 0), "empty chart never gives valid completed result");
}

void testDocumentSixNoteExample()
{
    GameEngine engine;
    engine.startSession(basicChart());
    engine.handleKeyPress(0, 1000);
    engine.handleKeyPress(0, 1000, true);
    engine.handleKeyRelease(0, 1001);
    engine.handleKeyPress(1, 2015);
    expect(engine.stats().finalizedCount == 1, "hold head does not add points");
    engine.advanceTo(3000);
    engine.handleKeyRelease(1, 3000);
    engine.handleKeyPress(2, 3480);
    engine.handleKeyRelease(2, 3481);
    engine.handleKeyPress(3, 3520);
    engine.handleKeyRelease(3, 3521);
    engine.handleKeyPress(0, 4180);
    engine.handleKeyRelease(0, 4181);
    engine.handleKeyPress(1, 4310);
    engine.handleKeyRelease(1, 4311);
    const auto result = ScoreService::buildResult(engine.stats(), 6);
    expect(engine.stats().perfect == 5 && engine.stats().good == 1 && engine.stats().miss == 0,
        "document four-kind sample statistics");
    expect(result.score == 900000 && result.grade == QStringLiteral("A") && result.fullCombo,
        "document sample score, grade and FC");
    expect(engine.finished() && engine.takeEvents().size() == 6, "exactly six final events");
    engine.advanceTo(9999);
    expect(engine.takeEvents().empty(), "later updates do not repeat final events");
}

void testEarliestCandidateAndChainMiss()
{
    Chart chart = oneNoteChart();
    chart.notes.push_back({2, NoteType::Tap, 0, 1200, 1200, -1, -1});
    GameEngine engine;
    engine.startSession(chart);
    engine.handleKeyPress(0, 1100);
    auto events = engine.takeEvents();
    expect(events.size() == 1 && events[0].noteId == 1, "one input chooses earliest allowed head");
    engine.handleKeyPress(0, 1200);
    expect(engine.stats().finalizedCount == 1, "held lane does not replace a second physical press");
    engine.handleKeyRelease(0, 1200);
    engine.handleKeyPress(0, 1201);
    expect(engine.stats().finalizedCount == 2, "release plus press can hit next head");

    chart.notes = {{1, NoteType::ChainStep, 0, 1000, 1000, 1, -1},
        {2, NoteType::ChainStep, 1, 1300, 1300, 1, -1}};
    engine.startSession(chart);
    engine.handleKeyPress(1, 1300);
    expect(engine.stats().miss == 1 && engine.stats().perfect == 1 && engine.stats().combo == 1,
        "missed chain step does not block later step");
}

void testWiderWindowsStayIndependent()
{
    GameEngine great;
    great.startSession(oneNoteChart());
    expect(great.handleKeyPress(0, 1100) && great.stats().great == 1,
        "100ms late input now earns Great");
    GameEngine good;
    good.startSession(oneNoteChart());
    expect(good.handleKeyPress(0, 1180) && good.stats().good == 1,
        "180ms late input remains playable with Good");

    Chart overlapping = oneNoteChart();
    overlapping.notes.push_back({2, NoteType::Tap, 0, 1200, 1200, -1, -1});
    GameEngine engine;
    engine.startSession(overlapping);
    engine.handleKeyPress(0, 1180);
    const auto first = engine.takeEvents();
    expect(first.size() == 1 && first[0].noteId == 1 && first[0].judgement == Judgement::Good,
        "overlapping wider windows choose the earliest note even when the next is closer");
    expect(engine.stats().finalizedCount == 1 && engine.stats().good == 1,
        "one physical press cannot consume both overlapping notes");
    engine.handleKeyPress(0, 1200, true);
    expect(engine.stats().finalizedCount == 1, "auto-repeat cannot consume the overlapping next note");
    engine.handleKeyRelease(0, 1200);
    expect(engine.handleKeyPress(0, 1201) && engine.stats().perfect == 1,
        "the overlapping next note still needs its own release and press");

    GameEngine hold;
    hold.startSession(oneNoteChart(NoteType::Hold));
    expect(hold.handleKeyPress(0, 1180) && hold.stats().finalizedCount == 0,
        "a relaxed Good hold head still defers scoring to the tail");
    hold.handleKeyRelease(0, 2000);
    expect(hold.stats().good == 1 && hold.stats().finalizedCount == 1,
        "wider hold head preserves the strict exact-tail rule and a single judgement");
}

void testPauseAndRestart()
{
    GameEngine engine;
    engine.startSession(oneNoteChart(NoteType::Hold));
    const QString previousSession = engine.sessionId();
    engine.handleKeyPress(0, 1000);
    engine.advanceTo(1500);
    expect(engine.pause(), "hold can pause");
    engine.handleKeyRelease(0, 9000);
    engine.advanceTo(9000);
    expect(engine.nowMs() == 1500 && engine.stats().finalizedCount == 0,
        "paused timeline and hold judgement are frozen");
    expect(engine.prepareResume(), "resume preparation starts");
    expect(!engine.resumeReady() && !engine.resume(), "active hold must be re-held");
    engine.resumeLanePress(0);
    expect(engine.resumeReady(), "all held lanes satisfy resume preparation");
    expect(engine.pause() && engine.state() == SessionState::Paused && !engine.laneDown()[0],
        "canceling resume preparation returns to paused with cleared physical keys");
    engine.advanceTo(9000);
    expect(engine.nowMs() == 1500 && engine.stats().finalizedCount == 0,
        "canceling resume keeps the timeline frozen");
    expect(engine.prepareResume() && !engine.resumeReady() && !engine.resume(),
        "continuing after cancellation still requires the original active hold");
    engine.resumeLanePress(0);
    engine.resumeLaneRelease(0);
    expect(!engine.resumeReady(), "release during resume preparation clears physical state");
    engine.resumeLanePress(0);
    expect(engine.resume(), "resumes when hold is re-held");
    engine.handleKeyRelease(0, 2000);
    expect(engine.stats().perfect == 1 && engine.stats().finalizedCount == 1,
        "resumed hold finishes once exactly at tail");
    engine.startSession(oneNoteChart());
    expect(engine.sessionId() != previousSession && engine.stats().finalizedCount == 0,
        "restart has fresh ID and empty statistics");
    expect(!engine.handleKeyPress(0, 999, true), "repeat cannot hit a fresh session");
    engine.advanceTo(1050);
    expect(!engine.handleKeyPress(0, 1000), "backdated events cannot hit earlier time");
}

void testStableAdvanceOrderingAndInvalidInput()
{
    Chart chart;
    chart.notes = {{3, NoteType::Tap, 3, 1000, 1000, -1, -1},
        {1, NoteType::Tap, 1, 1000, 1000, -1, -1},
        {2, NoteType::Tap, 2, 1000, 1000, -1, -1}};
    GameEngine engine;
    expect(engine.startSession(chart), "unsorted valid chart accepted");
    engine.advanceTo(2000);
    const auto events = engine.takeEvents();
    expect(events.size() == 3 && events[0].noteId == 1 && events[1].noteId == 2
            && events[2].noteId == 3,
        "simultaneous finalizations use time, lane and id stable ordering");
    chart.notes[0].lane = 4;
    expect(!engine.startSession(chart), "engine rejects out-of-range lane");
    expect(!engine.startSession({}), "engine rejects empty chart");
}

void testLateFramesKeepChronologicalCombo()
{
    Chart chart;
    chart.notes = {{1, NoteType::Hold, 0, 1000, 3000, -1, -1},
        {2, NoteType::Tap, 1, 2000, 2000, -1, -1}};
    GameEngine timely, late;
    timely.startSession(chart);
    late.startSession(chart);
    timely.handleKeyPress(0, 1000);
    late.handleKeyPress(0, 1000);
    timely.advanceTo(2221);
    timely.advanceTo(3000);
    late.advanceTo(3000);
    expect(late.stats().combo == timely.stats().combo && late.stats().combo == 1,
        "late timer batches use chronological finalization order");
    expect(late.stats().maxCombo == timely.stats().maxCombo,
        "late timer batches keep the same maximum combo");
    const auto events = late.takeEvents();
    expect(events.size() == 2 && events[0].noteId == 2 && events[0].musicTimeMs == 2221
            && events[1].noteId == 1 && events[1].musicTimeMs == 3000,
        "late events retain their effective judgement times");
}

} // namespace

int main()
{
    testWindowsAndRepeat();
    testExactWindowsAndOffset();
    testEarlyReleaseAndIndependentChords();
    testScoreAndGrade();
    testDocumentSixNoteExample();
    testEarliestCandidateAndChainMiss();
    testWiderWindowsStayIndependent();
    testPauseAndRestart();
    testStableAdvanceOrderingAndInvalidInput();
    testLateFramesKeepChronologicalCombo();
    if (failures != 0) {
        std::cerr << failures << " core test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All core tests passed\n";
    return EXIT_SUCCESS;
}
