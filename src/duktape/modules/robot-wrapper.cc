#include <fstream>
#include <iostream>
#include <string>
#include "robots.h"


extern "C" int rp_rbt_allowed(char *robots_txt, char *uagent, char *iurl );

int rp_rbt_allowed(char *robots_txt, char *uagent, char *iurl )
{
    googlebot::RobotsMatcher matcher;

    std::string robots_content(robots_txt);
    std::string url(iurl);
    std::string user_agent(uagent);
    std::vector<std::string> user_agents(1, user_agent);

    bool allowed = matcher.AllowedByRobots(robots_content, &user_agents, url);
    
    return allowed ? 1 : 0;
}

