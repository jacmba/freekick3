#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>

#include "common/Math.h"

#include "soccer/Match.h"

// #define DEBUG_SIMULATION

namespace Soccer {

MatchRules::MatchRules()
	: ExtraTimeOnTie(false),
	PenaltiesOnTie(false)
{
}

bool MatchResult::homeWon() const
{
	return Played && (HomeGoals > AwayGoals || HomePenalties > AwayPenalties);
}

bool MatchResult::awayWon() const
{
	return Played && (HomeGoals < AwayGoals || HomePenalties < AwayPenalties);
}

Match::Match(const boost::shared_ptr<StatefulTeam> t1, const boost::shared_ptr<StatefulTeam> t2,
		const MatchRules& r)
	: mTeam1(t1),
	mTeam2(t2),
	mRules(r)
{
}

MatchResult Match::play(bool display) const
{
	if(display) {
		RunningMatch rm = RunningMatch(*this);
		MatchResult r;
		while(!rm.matchFinished(&r)) {
			sleep(1);
		}
		return r;
	}
	else {
		return simulateMatchResult();
	}
}

MatchResult Match::simulateMatchResult() const
{
	SimulationStrength s1(*mTeam1);
	SimulationStrength s2(*mTeam2);
	return s1.simulateAgainst(s2, mRules);
}

SimulationStrength::SimulationStrength(const StatefulTeam& t)
	: mCenterDefense(0.0f),
	mCenterGet(0.0f),
	mCenterUse(0.0f),
	mLeftDefense(0.0f),
	mLeftGet(0.0f),
	mLeftUse(0.0f),
	mRightDefense(0.0f),
	mRightGet(0.0f),
	mRightUse(0.0f),
	mCenterTry(0.0f),
	mLeftTry(0.0f),
	mRightTry(0.0f),
	mLongBalls(0.0f)
{
	/* TODO: make use of FastPassing and ShootClose here. */
	float press = t.getTactics().Pressure * 0.5f + 0.25f;
	float mLongBalls = t.getTactics().LongBalls * 0.5f + 0.25f;
	float wings = 0.5f;
	float variance = 0.5f;

	press += (rand() % 2000 - 1000) * 0.001f * variance;
	mLongBalls += (rand() % 2000 - 1000) * 0.001f * variance;
	wings += (rand() % 2000 - 1000) * 0.001f * variance;

	press = Common::clamp(0.0f, press, 1.0f);
	mLongBalls = Common::clamp(0.0f, mLongBalls, 1.0f);
	wings = Common::clamp(0.25f, wings, 0.75f);

	for(auto p : t.getPlayers()) {
		auto it = t.getTactics().mTactics.find(p->getId());
		if(it == t.getTactics().mTactics.end())
			continue;

		if(it->second.Position == PlayerPosition::Goalkeeper) {
			mCenterDefense += p->getSkills().GoalKeeping;
			mLeftDefense   += p->getSkills().GoalKeeping;
			mRightDefense  += p->getSkills().GoalKeeping;
		}
		else {
			float generalplayerskill = (p->getSkills().Passing +
					p->getSkills().BallControl + p->getSkills().RunSpeed) / 3.0f;
			float def = 0.0f, get = 0.0f, use = 0.0f;
			if(it->second.Position == PlayerPosition::Defender) {
				def += p->getSkills().Tackling * generalplayerskill;
				get += p->getSkills().Passing * 0.5 * generalplayerskill;
			}
			else if(it->second.Position == PlayerPosition::Midfielder) {
				def += p->getSkills().Tackling * 0.25 * generalplayerskill;
				get += p->getSkills().Passing * generalplayerskill;
				use += p->getSkills().ShotPower * 0.25 * generalplayerskill;
			}
			else if(it->second.Position == PlayerPosition::Forward) {
				get += p->getSkills().Passing * 0.5 * generalplayerskill;
				use += p->getSkills().ShotPower * generalplayerskill;
			}

			get *= press;
			use *= (1.0f - press);

			// use this to tune goals/match
			use *= 4.0f;

#ifdef DEBUG_SIMULATION
			printf("Player %25s %3.2f %3.2f %3.2f = %3.2f %3.2f %3.2f = %3.2f %3.2f %3.2f\n",
					p->getName().c_str(),
					mLeftDefense, mCenterDefense, mRightDefense,
					mLeftGet, mCenterGet, mRightGet,
					mLeftUse, mCenterUse, mRightUse);
#endif
			float xpos = it->second.WidthPosition;
			float centered = 1.0f - fabs(xpos);
			centered = Common::clamp(0.0f, centered, 1.0f);
			bool left = xpos < 0.0f;
			mCenterDefense += def * centered;
			mCenterGet += get * centered;
			mCenterUse += use * centered;
			if(left) {
				mLeftDefense += def * (1.0f - centered);
				mLeftGet += get * (1.0f - centered);
				mLeftUse += use * (1.0f - centered);
			}
			else {
				mRightDefense += def * (1.0f - centered);
				mRightGet += get * (1.0f - centered);
				mRightUse += use * (1.0f - centered);
			}
		}
	}

	mCenterTry = (mCenterGet) * (1.0f - wings);
	mLeftTry   = (mLeftGet)   * (0.5f * wings);
	mRightTry  = (mRightGet)  * (0.5f * wings);
}

int SimulationStrength::pickOne(const std::vector<float>& values)
{
	float total = 0.0f;
	for(auto t : values) {
		total += t;
	}

	float randvalue = (rand() % 10000) / 10000.0f;
	float sum = 0.0f;
	int i = 0;
	for(auto t : values) {
		sum += t;
		if(randvalue * total < sum) {
			return i;
		}
		i++;
	}
	return values.size() - 1;
}

MatchResult SimulationStrength::simulateAgainst(const SimulationStrength& t2, const MatchRules& r)
{
	const int steps = 9;
	int homegoals = 0, awaygoals = 0;

	float totalTry = mCenterTry + mLeftTry + mRightTry +
		t2.mCenterTry + t2.mLeftTry + t2.mRightTry;
	float centerTry = (mCenterTry + t2.mCenterTry) / totalTry;
	float leftTry = (mLeftTry + t2.mRightTry)      / totalTry;
	float rightTry = (mRightTry + t2.mLeftTry)     / totalTry;
	std::vector<float> tries;
	tries.push_back(leftTry);
	tries.push_back(centerTry);
	tries.push_back(rightTry);

	for(int i = 0; i < steps; i++) {
		simulateStep(t2, homegoals, awaygoals, tries);
	}

	if(homegoals == awaygoals && r.ExtraTimeOnTie) {
		for(int i = 0; i < 3; i++) {
			simulateStep(t2, homegoals, awaygoals, tries);
		}
	}

	if(homegoals == awaygoals && r.PenaltiesOnTie) {
		int homepen = rand() % 3 + 3;
		int awaypen = rand() % 3 + 3;
		if(homepen == awaypen) {
			int h = rand() % 2;
			if(h)
				homepen++;
			else
				awaypen++;
		}
		return MatchResult(homegoals, awaygoals, homepen, awaypen);
	} else {
		return MatchResult(homegoals, awaygoals);
	}

}

void SimulationStrength::simulateStep(const SimulationStrength& t2, int& homegoals, int& awaygoals, const std::vector<float>& tries)
{
#ifdef DEBUG_SIMULATION
	printf("Step ");
#endif

	int trynum = pickOne(tries);
	float t1get, t2get;
	float t1def, t1att, t2def, t2att;
	if(trynum == 0) {
		t1get = mLeftGet;
		t1def = mLeftDefense;
		t1att = mLeftUse;
		t2get = t2.mRightGet;
		t2def = t2.mRightDefense;
		t2att = t2.mRightUse;
#ifdef DEBUG_SIMULATION
		printf("left ");
#endif
	}
	else if(trynum == 1) {
		t1get = mCenterGet;
		t1def = mCenterDefense;
		t1att = mCenterUse;
		t2get = t2.mCenterGet;
		t2def = t2.mCenterDefense;
		t2att = t2.mCenterUse;
#ifdef DEBUG_SIMULATION
		printf("center ");
#endif
	}
	else {
		t1get = mRightGet;
		t1def = mRightDefense;
		t1att = mRightUse;
		t2get = t2.mLeftGet;
		t2def = t2.mLeftDefense;
		t2att = t2.mLeftUse;
#ifdef DEBUG_SIMULATION
		printf("right ");
#endif
	}

	int holdnum;
	if(t1get && !t2get) {
		holdnum = 0;
	}
	else if(t2get && !t1get) {
		holdnum = 1;
	}
	else {
		float t1hold = t1get / (t1get + t2get);
		float difftomiddle = t1hold - ((t1get + t2get) / 2.0f);
		float totalLongBalls = Common::clamp(0.0f, (mLongBalls + t2.mLongBalls) / 2.0f, 1.0f);
		t1hold -= difftomiddle * totalLongBalls;
		std::vector<float> holding;
		holding.push_back(t1get - difftomiddle * totalLongBalls);
		holding.push_back(t2get + difftomiddle * totalLongBalls);
		holdnum = pickOne(holding);
	}
	float att, def;
	bool homescorer;
	if(holdnum == 0) {
#ifdef DEBUG_SIMULATION
		printf("home ");
#endif
		att = t1att;
		def = t2def;
		homescorer = true;
	}
	else {
#ifdef DEBUG_SIMULATION
		printf("away ");
#endif
		att = t2att;
		def = t1def;
		homescorer = false;
	}
	std::vector<float> scoring;
	scoring.push_back(att);
	scoring.push_back(def);
	int scorenum = pickOne(scoring);
	if(scorenum == 0) {
		if(homescorer) {
			homegoals++;
		}
		else {
			awaygoals++;
		}
#ifdef DEBUG_SIMULATION
		printf("scores! %d-%d\n", homegoals, awaygoals);
#endif
	}
	else {
#ifdef DEBUG_SIMULATION
		printf("blocked\n");
#endif
	}
}

const MatchResult& Match::getResult() const
{
	return mResult;
}

void Match::setResult(const MatchResult& m)
{
	mResult = m;
}

const boost::shared_ptr<StatefulTeam> Match::getTeam(int i) const
{
	if(i == 0)
		return mTeam1;
	else
		return mTeam2;
}

const MatchRules& Match::getRules() const
{
	return mRules;
}

Match::Match()
	: mRules(false, false)
{
}

RunningMatch::RunningMatch(const Match& m)
{
	tmpnam(matchfilenamebuf);
	DataExchange::createMatchDataFile(m, matchfilenamebuf);
	std::cout << "Created temporary file " << matchfilenamebuf << "\n";
	int teamnum = 0;
	int plnum = 0;
	if(m.getTeam(0)->getController().HumanControlled && !m.getTeam(1)->getController().HumanControlled) {
		teamnum = 1;
		plnum = m.getTeam(0)->getController().PlayerShirtNumber;
	}
	else if(!m.getTeam(0)->getController().HumanControlled && m.getTeam(1)->getController().HumanControlled) {
		teamnum = 2;
		plnum = m.getTeam(1)->getController().PlayerShirtNumber;
	}
	startMatch(teamnum, plnum);
}

void RunningMatch::startMatch(int teamnum, int playernum)
{
	pid_t fret = fork();
	if(fret == 0) {
		/* child */
		std::vector<const char*> args;
		args.push_back("freekick3-match");
		args.push_back(matchfilenamebuf);
		if(teamnum == 0) {
			args.push_back("-o");
		}
		else {
			args.push_back("-t");
			args.push_back(std::to_string(teamnum).c_str());
			if(playernum != 0) {
				args.push_back("-p");
				args.push_back(std::to_string(playernum).c_str());
			}
		}

		std::cout << "Running command: ";
		for(auto arg : args) {
			std::cout << arg << " ";
		}
		std::cout << "\n";

		args.push_back((char*)0);

		char* const* argsarray = const_cast<char* const*>(&args[0]);

		if(execvp("freekick3-match", argsarray) == -1) {
			/* try bin/freekick3-match */
			char cwdbuf[256];
			if(getcwd(cwdbuf, 256) == NULL) {
				perror("getcwd");
				exit(1);
			}
			else {
				std::string fullpath(cwdbuf);
				fullpath += "/bin/freekick3-match";
				if(execv(fullpath.c_str(), argsarray) == -1) {
					perror("execl");
					fprintf(stderr, "tried running %s\n", fullpath.c_str());
					exit(1);
				}
			}
		}
	}
	else if(fret != -1) {
		/* parent */
		mChildPid = fret;
		return;
	}
	else {
		perror("fork");
		throw std::runtime_error("fork() failed");
	}
}

bool RunningMatch::matchFinished(MatchResult* r)
{
	assert(r);
	pid_t waited = waitpid(mChildPid, NULL, WNOHANG);
	if(waited == -1) {
		perror("waitpid");
	}
	if(waited == mChildPid) {
		boost::shared_ptr<Match> match = DataExchange::parseMatchDataFile(matchfilenamebuf);
		unlink(matchfilenamebuf);
		*r = match->getResult();
		return true;
	}
	else {
		return false;
	}
}

}


