#include <iostream>
#include <string>
#include <fstream>
#include <ctime>
#include <regex>
#include <dirent.h>
#include <unistd.h>
#include "str_tok.h"
#include "halfmod.h"
#include "halfmod_func.h"
#include ".hmEngineBuild.h"
using namespace std;

int loadPlugin(hmGlobal &info, vector<hmHandle> &plugins, string path)
{
	for (auto it = plugins.begin(), ite = plugins.end();it != ite;++it)
	{
		if (it->getPath() == path)
		{
			cerr<<"Plugin \""<<path<<"\" is already loaded."<<endl;
			return 3;
		}
	}
	hmHandle temp;
	if (!temp.load(path,&info))
	{
		cerr<<"Error loading plugin \""<<path<<"\" . . .\n";
		return 1;
	}
	else
	{
		if (temp.getAPI() != API_VERSION)
		{
			cout<<"Error plugin \""<<path<<"\" was compiled with a different version of the API ("<<temp.getAPI()<<") . . . Recompile with "<<API_VERSION<<".\n";
			return 2;
		}
		else
		{
			plugins.push_back(temp);
			cout<<"Plugin \""<<plugins[plugins.size()-1].getInfo().name<<"\" loaded . . .\n";
		}
	}
	return 0;
}

int readSock(int sock, string &buffer)
{
	buffer = "";
	char c[1];
	int s = 0;
	while (read(sock,c,1) > 0)
	{
		if ((c[0] == '\r') || (c[0] == '\n'))
		{
			if (!s) s = -1;
			break;
		}
		buffer += c[0];
		s++;
	}
	return s;
}

int findPlugins(const char *dir, vector<string> &paths)
{
	DIR *wd;
	struct dirent *entry;
	if ((wd = opendir(dir)))
	{
		while ((entry = readdir(wd)))
			if (strright(entry->d_name,4) == ".hmo")
				paths.push_back(dir + string(entry->d_name));
		closedir(wd);
	}
	return paths.size();
}

// in charge of populating the hmGlobal struct and controlling console output
// I said f it and made it handle everything.
int processThread(hmGlobal &info, vector<hmHandle> &plugins, vector<hmConsoleFilter> &filters, string thread)
{
	static bool catchLine = false;
	bool out = true;
	if (catchLine == true)
	{
		catchLine = false;
		string nextLine;
		nextLine = strremove(deltok(deltok(deltok(thread,1," "),1," "),1," "),",");
		for (int i = 1, j = numtok(nextLine," ");i <= j;i++)
			loadPlayerData(info,gettok(nextLine,i," "));
	}
	for (auto it = filters.begin(), ite = filters.end();it != ite;++it)
	{
		if (regex_search(thread,it->filter))
		{
			if (it->blocking)
				return 1;
			out = false;
		}
	}
	if (out)
		cout<<thread<<endl;
    if (processHooks(plugins,thread))
        return 1;
    regex ptrn ("\\[[0-9]{2}:[0-9]{2}:[0-9]{2}\\] (.+)");
    smatch ml;
    if (regex_match(thread,ml,ptrn))
    {
        thread = ml[1];
        ptrn = "\\[Server thread/INFO\\]: (.*)";
        if (regex_match(thread,ml,ptrn))
        {
            if (processEvent(plugins,HM_ONINFO,ml))
                return 1;
            thread = ml[1];
            ptrn = "[^\\s\\[\\]<>]+ did not match.*";
            if (regex_match(thread,ptrn))
                return 0;
            ptrn = "<(\\S+?)> (.*)";
            if (regex_match(thread,ml,ptrn))
            {
                if (hmIsPlayerOnline(ml[1]))
                {
                    smatch ml1 = ml;
                    ptrn = "<(\\S+?)> !(\\S+) ?(.*)";
                    if (regex_match(thread,ml,ptrn))
                    {
                            if (processCmd(info,plugins,filters,"hm_" + ml[2].str(),ml[1],ml[3]))
                                return 1;
                    }
                    else if (processEvent(plugins,HM_ONTEXT,ml1))
                        return 1;
                }
                else if (processEvent(plugins,HM_ONFAKETEXT,ml))
                    return 1;
            }
            else
            {
                ptrn = "\\[(\\S+)\\] (.*)";
                if (regex_match(thread,ml,ptrn))
                {
                    if (processEvent(plugins,HM_ONFAKETEXT,ml))
                        return 1;
                }
                else
                {
                    ptrn = "\\* (\\S+) (.+)";
                    if (regex_match(thread,ml,ptrn))
                    {
                        if (processEvent(plugins,HM_ONACTION,ml))
                            return 1;
                    }
                    else
                    {
                        ptrn = "([^\\s\\[]+)\\[/([0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}):([0-9]{1,})\\] logged in.*";
                        if (regex_match(thread,ml,ptrn))
                        {
                            fstream file ("./halfMod/userdata/" + stripFormat(lower(ml[1])) + ".dat",ios_base::in);
						    string line, lines;
						    lines = addtok(lines,"ip=" + string(ml[2]),"\n");
    					    lines = addtok(lines,data2str("join=%li",time(NULL)),"\n");
						    if (file.is_open())
						    {
							    while (getline(file,line))
							    {
								    if (!istok("ip join",gettok(line,1,"=")," "))
									    lines = addtok(lines,line,"\n");
							    }
							    file.close();
						    }
						    file.open("./halfMod/userdata/" + stripFormat(lower(ml[1])) + ".dat",ios_base::out|ios_base::trunc);
						    if (file.is_open())
						    {
							    file<<lines;
							    file.close();
						    }
						    if (processEvent(plugins,HM_ONCONNECT,ml))
                                return 1;
                        }
                        else
                        {
                            ptrn = "Starting minecraft server version (.+)";
                            if (regex_match(thread,ml,ptrn))
                            {
                                info.mcVer = ml[1];
                                info.players.clear();
                            }
                            else
                            {
                                ptrn = "Preparing level (.+)";
                                if (regex_match(thread,ml,ptrn))
                                    info.world = ml[1];
                                else
                                {
                                    ptrn = "Done \\((.*)\\)!.*";
                                    if (regex_match(thread,ml,ptrn))
                                    {
                                        if (info.mcVer == "")
					                        cout<<"Minecraft version undefined."<<endl;
				                        else
				                            cout<<"Minecraft version found: "<<info.mcVer<<endl;
				                        if (info.world == "")
					                        cout<<"World undefined."<<endl;
				                        else
				                            cout<<"World defined as: "<<info.world<<endl;
				                        internalExec(info,plugins,filters,"","#SERVER","autoexec");	// on world load setup
				                        internalExec(info,plugins,filters,"","#SERVER","startup");	// this will contain timed unbans if the server was offline when they expired
				                        remove("./halfMod/configs/startup.cfg");	// delete startup
				                        if (processEvent(plugins,HM_ONWORLDINIT,ml))
					                        return 1;
					                }
					                else
					                {
					                    ptrn = "There are ([0-9]{1,})/([0-9]{1,}) players online:";
					                    if (regex_match(thread,ml,ptrn))
					                    {
					                        info.players.clear();
					                        if (stoi(ml[1].str()) > 0)
						                        catchLine = true;
						                    info.maxPlayers = stoi(ml[2].str());
						                }
						                else
						                {
						                    ptrn = "(\\S+) joined the game";
						                    if (regex_match(thread,ml,ptrn))
						                    {
						                        loadPlayerData(info,ml[1]);
								                if (processEvent(plugins,HM_ONJOIN,ml))
									                return 1;
									        }
									        else
									        {
									            if (hmIsPlayerOnline(gettok(thread,1," ")))
									            {
									                ptrn = "(\\S+) lost connection: (.*)";
									                if (regex_match(thread,ml,ptrn))
									                {
									                    fstream file ("./halfMod/userdata/" + stripFormat(lower(ml[1])) + ".dat",ios_base::in);
									                    string line, lines;
									                    lines = addtok(lines,data2str("quit=%li",time(NULL)),"\n");
									                    lines = addtok(lines,"quitmsg=" + string(ml[2]),"\n");
									                    if (file.is_open())
									                    {
										                    while (getline(file,line))
										                    {
											                    if (!istok("quit quitmsg",gettok(line,1,"=")," "))
												                    lines = addtok(lines,line,"\n");
										                    }
										                    file.close();
									                    }
									                    file.open("./halfMod/userdata/" + stripFormat(lower(ml[1])) + ".dat",ios_base::out|ios_base::trunc);
									                    if (file.is_open())
									                    {
										                    file<<lines;
										                    file.close();
									                    }
									                    if (processEvent(plugins,HM_ONDISCONNECT,ml))
										                    return 1;
										            }
										            else
										            {
										                ptrn = "(\\S+) left the game";
										                if (regex_match(thread,ml,ptrn))
										                {
										                    for (auto it = info.players.begin();it != info.players.end();)
										                    {
											                    if (stripFormat(lower(it->name)) == stripFormat(lower(ml[1])))
												                    info.players.erase(it);
											                    else
												                    ++it;
										                    }
										                    if (processEvent(plugins,HM_ONPART,ml))
											                    return 1;
											            }
											            else
											            {
											                ptrn = "(\\S+) has made the advancement (.*)";
											                if (regex_match(thread,ml,ptrn))
											                {
											                    if (processEvent(plugins,HM_ONADVANCE,ml))
											                        return 1;
											                }
											                else
											                {
											                    ptrn = "(\\S+) has reached the goal (.*)";
											                    if (regex_match(thread,ml,ptrn))
											                    {
											                        if (processEvent(plugins,HM_ONGOAL,ml))
											                            return 1;
											                    }
											                    else
											                    {
											                        ptrn = "(\\S+) has completed the challenge (.*)";
											                        if (regex_match(thread,ml,ptrn))
											                        {
											                            if (processEvent(plugins,HM_ONCHALLENGE,ml))
											                                return 1;
											                        }
											                        else
											                        {
											                            ptrn = "(\\S+) (.+)";
											                            if (regex_match(thread,ml,ptrn))
											                            {
											                                string client = stripFormat(lower(ml[1].str())), msg = ml[2].str();
											                                time_t cTime = time(NULL);
											                                fstream file ("./halfMod/userdata/" + client + ".dat",ios_base::in);
												                            string line, lines;
												                            lines = addtok(lines,data2str("death=%li",cTime),"\n");
												                            lines = addtok(lines,"deathmsg=" + msg,"\n");
												                            if (file.is_open())
												                            {
													                            while (getline(file,line))
													                            {
														                            if (!istok("death deathmsg",gettok(line,1,"=")," "))
															                            lines = addtok(lines,line,"\n");
													                            }
													                            file.close();
												                            }
												                            file.open("./halfMod/userdata/" + client + ".dat",ios_base::out|ios_base::trunc);
												                            if (file.is_open())
												                            {
													                            file<<lines;
													                            file.close();
												                            }
												                            for (auto it = info.players.begin(), ite = info.players.end();it != ite;++it)
												                            {
												                                if (stripFormat(lower(it->name)) == client)
												                                {
												                                    it->death = cTime;
												                                    it->deathmsg = msg;
												                                    break;
												                                }
												                            }
												                            if (processEvent(plugins,HM_ONDEATH,ml))
													                            return 1;
													                    }
													                }
													            }
													        }
													    }
													}
												}
											}
										}
									}
								}
							}
						}
					}
				}
            }
        }
        else
        {
            ptrn = "\\[Server thread/WARN\\]: (.*)";
            if (regex_match(thread,ml,ptrn))
            {
                if (processEvent(plugins,HM_ONWARN,ml))
				    return 1;
            }
            else
            {
                ptrn = "\\[Server Shutdown Thread/INFO\\]: (.*)";
                if (regex_match(thread,ml,ptrn))
                {
                    if (processEvent(plugins,HM_ONSHUTDOWN,ml))
                        return 1;
                }
                ptrn = "\\[User Authenticator #([0-9]{1,})/INFO\\]: UUID of player (\\S+) is (.*)";
                if (regex_match(thread,ml,ptrn))
                {
                    string client = stripFormat(lower(ml[2]));
                    fstream file ("./halfMod/userdata/" + client + ".dat",ios_base::in);
					string line, lines;
					bool needWrite = true;
					lines = "uuid=" + ml[3].str() + "\n";
					if (file.is_open())
					{
						while (getline(file,line))
						{
							if (line == "uuid=" + ml[3].str())
							{
								needWrite = false;
								break;
							}
							else if (gettok(line,1,"=") != "uuid")
								lines = addtok(lines,line,"\n");
						}
						file.close();
					}
					if (needWrite)
					{
						file.open("./halfMod/userdata/" + client + ".dat",ios_base::out|ios_base::trunc);
						if (file.is_open())
						{
							file<<lines;
							file.close();
						}
					}
                    if (processEvent(plugins,HM_ONAUTH,ml))
                        return 1;
                }
            }
        }
    }
    return 0;
}

int processEvent(vector<hmHandle> &plugins, int event, smatch thread)
{
	hmEvent evnt;
	for (auto it = plugins.begin(), ite = plugins.end();it != ite;++it)
	{
		evnt = it->findEvent(event);
		if (evnt.event > 0)
			if ((*evnt.func)(*it,thread))
				return 1;
	}
	return 0;
}

int isInternalCmd(string cmd)
{
	static string cmdlist[] =
	{
		"hm",
		"hm_reloadadmins",
		"hm_info",
		"hm_version",
		"hm_credits",
		"hm_exec",
		"hm_rcon",
		"hm_rehashfilter"
	};
	for (int i = 0;i < 8;i++)
	{
		if (cmdlist[i] == cmd)
			return i;
	}
	return -1;
}

int processCmd(hmGlobal &global, vector<hmHandle> &plugins, vector<hmConsoleFilter> &filters, string cmd, string caller, string args, bool console)
{
	if (caller != "#SERVER")
		cout<<"[HM] "<<caller<<" is issuing the command "<<cmd<<endl;
	hmCommand com;
	int flags;
	if (console)
		flags = FLAG_ROOT;
	else	flags = hmGetPlayerFlags(caller);
	string *arga;
	int argc = numtok(args," ")+1;
	arga = new string[argc];
	arga[0] = cmd;
	for (int i = 1;i < argc;i++)
		arga[i] = gettok(args,i," ");
	int ret = 0, internal;
	if ((internal = isInternalCmd(cmd)) > -1)
	{
		switch (internal)
		{
			case 0:
			{
				ret = internalHM(global,plugins,caller,arga,argc);
				break;
			}
			case 1:
			{
				ret = internalReload(global,caller);
				break;
			}
			case 2:
			{
				ret = internalInfo(plugins,caller,arga,argc);
				break;
			}
			case 3:
			{
				ret = internalVersion(global,caller);
				break;
			}
			case 4:
			{
				ret = internalCredits(global,caller);
				break;
			}
			case 5:
			{
				ret = internalExec(global,plugins,filters,cmd,caller,args);
				break;
			}
			case 6:
			{
				ret = internalRcon(global,plugins,filters,cmd,caller,args);
				break;
			}
			case 7:
			{
				ret = internalRehash(filters,caller);
				break;
			}
		}
	}
	else
	{
		for (auto it = plugins.begin(), ite = plugins.end();it != ite;++it)
		{
			com = it->findCmd(cmd);
			if (com.cmd != "")
			{
				if ((com.flag == 0) || (flags & com.flag))
				{
					ret = (*com.func)(*it,caller,arga,argc);
					internal = 1;
					break;
				}
				else
				{
					hmReplyToClient(caller,"You do not have access to this command.");
					internal = 1;
					ret = 1;
					break;
				}
			}
		}
		if ((console) && (internal != 1))
		{
			hmSendRaw(cmd + " " + args,false);
			ret = 1;
		}
	}
	delete[] arga;
	return ret;
}

int processHooks(vector<hmHandle> &plugins, string thread)
{
	smatch ml;
	for (auto ita = plugins.begin(), itb = plugins.end();ita != itb;++ita)
	{
		for (auto it = ita->hooks.begin(), ite = ita->hooks.end();it != ite;++it)
		{
			if (it->name != "")
				if (regex_search(thread,ml,regex(it->ptrn)))
					if ((*it->func)(*ita,*it,ml))
						return 1;
		}
	}
	return 0;
}

int hashAdmins(hmGlobal &info, string path)
{
	// faster than looping through a mult table for each and every value.
	int FLAGS[26] = { 1,2,4,8,16,32,64,128,256,512,1024,2048,4096,8192,16384,32768,65536,131072,262144,524288,1048576,2097152,4194304,8388608,16777216,33554431 };
	ifstream file (path);
	string line, flagstr;
	int flags;
	regex comment ("#.*");
	regex white ("\\s");
	for (auto it = info.players.begin(), ite = info.players.end();it != ite;++it)
	    it->flags = 0;
	if (file.is_open())
	{
		info.admins.clear();
		while (getline(file,line))
		{
			line = regex_replace(line,white,"");
			if (regex_match(line,comment))
				continue;
			line = regex_replace(line,comment,"");
			if (numtok(line,"=") > 1)
			{
				flags = 0;
				flagstr = lower(deltok(line,1,"="));
				for (int i = 0;i < flagstr.size();i++)
				{
					if (!isalpha(flagstr[i]))
						continue;
					flags |= FLAGS[flagstr[i]-97];
				}
				info.admins.push_back({ gettok(line,1,"="), flags });
				for (auto it = info.players.begin(), ite = info.players.end();it != ite;++it)
				{
				    if (lower(gettok(line,1,"=")) == stripFormat(lower(it->name)))
				        it->flags = flags;
				}
			}
		}
		file.close();
		return 0;
	}
	return 1;
}

int hashConsoleFilters(vector<hmConsoleFilter> &filters, string path)
{
	ifstream file(path);
	if (file.is_open())
	{
		filters.clear();
		string line;
		hmConsoleFilter temp;
		while (getline(file,line))
		{
			if (gettok(line,1," ") == "0")
				temp.blocking = false;
			else	temp.blocking = true;
			temp.filter = deltok(line,1," ");
			filters.push_back(temp);
		}
		file.close();
		return 0;
	}
	return 1;
}

void loadPlayerData(hmGlobal &info, string name)
{
	for (auto it = info.players.begin();it != info.players.end();)
	{
		if (stripFormat(lower(it->name)) == stripFormat(lower(name)))
			it = info.players.erase(it);
		else
			++it;
	}
	info.players.push_back(hmGetPlayerData(name));
}

int internalHM(hmGlobal &global, vector<hmHandle> &plugins, string caller, string args[], int argc)
{
	if (argc < 2)
		internalVersion(global,caller);
	else if (argc < 3)
	{
		hmReplyToClient(caller,"Usage: " + args[0] + " plugins list");
		hmReplyToClient(caller,"Usage: " + args[0] + " plugins info <plugin>");
		hmReplyToClient(caller,"Usage: " + args[0] + " plugins load <plugin>");
		hmReplyToClient(caller,"Usage: " + args[0] + " plugins unload <plugin>");
		hmReplyToClient(caller,"Usage: " + args[0] + " plugins reload <plugin>");
	}
	else
	{
		int n = 0;
		if (args[1] + " " + args[2] == "plugins list")
		{
			hmReplyToClient(caller,"The following plugins are loaded:");
			for (auto it = plugins.begin(), ite = plugins.end();it != ite;++it)
			{
				hmReplyToClient(caller,data2str("[%i] %s identifies as '%s'",n,it->getPlugin().c_str(),it->getInfo().name.c_str()));
				n++;
			}
		}
		else if (args[1] + " " + args[2] == "plugins info")
		{
			if (argc < 4)
				hmReplyToClient(caller,"Usage: " + args[0] + " plugins info <plugin>");
			else
			{
				bool found = false;
				for (auto it = plugins.begin(), ite = plugins.end();it != ite;++it)
				{
					if ((it->getPlugin() == args[3]) || (it->getPath() == args[3]) || ((stringisnum(args[3],0)) && (atoi(args[3].c_str()) == n)))
					{
						found = true;
						hmReplyToClient(caller,data2str("[%i] Plugin %s:",n,it->getPlugin().c_str()));
						hmReplyToClient(caller,"    Path   : " + it->getPath());
						hmInfo temp = it->getInfo();
						hmReplyToClient(caller,"    Name   : " + temp.name);
						hmReplyToClient(caller,"    Author : " + temp.author);
						hmReplyToClient(caller,"    Desc   : " + temp.description);
						hmReplyToClient(caller,"    Version: " + temp.version);
						hmReplyToClient(caller,"    Url    : " + temp.url);
						hmReplyToClient(caller,data2str("    %i registered command(s).",it->totalCmds()));
						hmReplyToClient(caller,data2str("    %i registered event(s).",it->totalEvents()));
						hmReplyToClient(caller,data2str("    %lu registered hook(s).",it->hooks.size()));
						hmReplyToClient(caller,data2str("    %lu running timer(s).",it->timers.size()));
					}
					n++;
				}
				if (!found)
					hmReplyToClient(caller,"Could not find any loaded plugins for the given criteria.");
			}
		}
		else if (args[1] + " " + args[2] == "plugins load")
		{
			if (argc < 4)
				hmReplyToClient(caller,"Usage: " + args[0] + " plugins load <plugin>");
			else
			{
				string path = "./halfMod/plugins/" + args[3] + ".hmo";
				int err = loadPlugin(global,plugins,path);
				if (caller != "#SERVER")
				{
					switch (err)
					{
						case 0: // success
						{
							hmReplyToClient(caller,"Plugin '" + plugins[plugins.size()-1].getInfo().name + "' loaded successfully.");
							break;
						}
						case 1: // not loaded
						{
							hmReplyToClient(caller,"Error loading plugin '" + path + "'.");
							break;
						}
						case 2: // api mismatch
						{
							hmReplyToClient(caller,"Error plugin '" + path + "' was compiled with a different version of the API . . . Recompile with " + string(API_VERSION));
							break;
						}
						case 3: // already loaded
						{
							hmReplyToClient(caller,"Plugin '" + path + "' is already loaded.");
							break;
						}
					}
				}
			}
		}
		else if (args[1] + " " + args[2] == "plugins unload")
		{
			if (argc < 4)
				hmReplyToClient(caller,"Usage: " + args[0] + " plugins unload <plugin>");
			else
			{
				bool found = false;
				for (auto it = plugins.begin();it != plugins.end();)
				{
					if ((it->getPlugin() == args[3]) || (it->getPath() == args[3]) || ((stringisnum(args[3],0)) && (stoi(args[3]) == n)))
					{
						found = true;
						it->unload();
						//it->hooks.clear();
						it = plugins.erase(it);
						hmReplyToClient(caller,"Plugin " + args[3] + " unloaded.");
					}
					else	++it;
					n++;
				}
				if (!found)
					hmReplyToClient(caller,"Could not find any loaded plugins for the given criteria.");
			}
		}
		else if (args[1] + " " + args[2] == "plugins reload")
		{
			if (argc < 4)
				hmReplyToClient(caller,"Usage: " + args[0] + " plugins reload <plugin>");
			else
			{
				bool found = false;
				string path;
				for (auto it = plugins.begin();it != plugins.end();)
				{
					if ((it->getPlugin() == args[3]) || (it->getPath() == args[3]) || ((stringisnum(args[3],0)) && (atoi(args[3].c_str()) == n)))
					{
						found = true;
						it->unload();
						path = it->getPath();
						it = plugins.erase(it);
						break;
					}
					else    ++it;
					n++;
				}
				if (!found)
					hmReplyToClient(caller,"Could not find any loaded plugins for the given criteria.");
				else
				{
					int err = loadPlugin(global,plugins,path);
					if (caller != "#SERVER")
					{
						switch (err)
						{
							case 0: // success
							{
								hmReplyToClient(caller,"Plugin '" + plugins[plugins.size()-1].getInfo().name + "' reloaded successfully.");
								break;
							}
							case 1: // not loaded
							{
								hmReplyToClient(caller,"Error loading plugin '" + path + "'.");
								break;
							}
							case 2: // api mismatch
							{
								hmReplyToClient(caller,"Error plugin '" + path + "' was compiled with a different version of the API . . . Recompile with " + string(API_VERSION));
								break;
							}
							case 3: // already loaded
							{
								hmReplyToClient(caller,"Plugin '" + path + "' is already loaded. Then how was it loaded in the first place??");
								break;
							}
						}
					}
				}
			}
		}
		else
		{
			hmReplyToClient(caller,"Usage: " + args[0] + " plugins list");
			hmReplyToClient(caller,"Usage: " + args[0] + " plugins info <plugin>");
			hmReplyToClient(caller,"Usage: " + args[0] + " plugins load <plugin>");
			hmReplyToClient(caller,"Usage: " + args[0] + " plugins unload <plugin>");
			hmReplyToClient(caller,"Usage: " + args[0] + " plugins reload <plugin>");
		}
	}
	return 0;
}

int internalReload(hmGlobal &global, string caller)
{
	if ((hmGetPlayerFlags(caller) & FLAG_CONFIG) == 0)
	{
		hmReplyToClient(caller,"You do not have access to this command.");
		return 9;
	}
	if (hashAdmins(global,ADMINCONF))
	{
		hmReplyToClient(caller,"Unable to load admin config file . . .");
		return 1;
	}
	hmReplyToClient(caller,"Successfully reloaded the admin config file.");
	return 0;
}

int internalExec(hmGlobal &global, vector<hmHandle> &plugins, vector<hmConsoleFilter> &filters, string cmd, string caller, string args)
{
	if ((hmGetPlayerFlags(caller) & FLAG_CONFIG) == 0)
	{
		hmReplyToClient(caller,"You do not have access to this command.");
		return 9;
	}
	if (args == "")
	{
		hmReplyToClient(caller,"Usage: " + cmd + " <cfg file name>");
		return 8;
	}
	ifstream file ("./halfMod/configs/" + args + ".cfg");
	if (!file.is_open())
	{
		hmReplyToClient(caller,"Error: Cannot open file './halfMod/configs/" + args + ".cfg'");
		return 1;
	}
	string line;
	int lines = 0;
	for (;getline(file,line);lines++)
		processCmd(global,plugins,filters,gettok(line,1," "),"#SERVER",deltok(line,1," "),true);
	hmReplyToClient(caller,data2str("Executed %i line(s) from file.",lines));
	file.close();
	return 0;
}

int internalInfo(vector<hmHandle> &plugins, string caller, string args[], int argc)
{
    if ((hmGetPlayerFlags(caller) & FLAG_ADMIN) == 0)
    {
        hmReplyToClient(caller,"You do not have access to this command.");
        return 9;
    }
    int plug = -1;
    string criteria = "";
    int page = 1;
    if (argc > 1)
    {
        if ((args[1] == "plugin") && (argc > 2))
        {
            bool found = false;
            for (auto it = plugins.begin();it != plugins.end();++it)
			{
			    plug++;
				if ((it->getPlugin() == args[2]) || (it->getPath() == args[2]) || ((stringisnum(args[2],0)) && (stoi(args[2]) == plug)))
				{
				    hmReplyToClient(caller,"Displaying commands from plugin: '" + it->getPlugin() + "'");
			        found = true;
			        break;
			    }
		    }
		    if (!found)
		    {
		        hmReplyToClient(caller,"Could not find any loaded plugins for the given criteria.");
		        return 3;
	        }
	        if ((argc > 3) && (stringisnum(args[3],1)))
	            page = stoi(args[3]);
        }
        else
        {
            if ((argc > 2) && (stringisnum(args[2],1)))
            {
                criteria = args[1];
                page = stoi(args[2]);
            }
            else if (stringisnum(args[1],1))
                page = stoi(args[1]);
            else
                criteria = args[1];
        }
    }
    int n = -1;
    vector<string> list;
    string entry;
    for (auto it = plugins.begin(), ite = plugins.end();it != ite;++it)
    {
        n++;
        if ((plug > -1) && (n != plug))
            continue;
        for (auto com = it->cmds.begin(), come = it->cmds.end();com != come;++com)
        {
            if (criteria.size() > 0)
            {
                if ((!isin(com->cmd,criteria)) && (!isin(com->desc,criteria)))
                    continue;
            }
            entry = com->cmd + " - " + com->desc;
            list.push_back(entry);
        }
    }
    if (list.size() < 1)
    {
        if (plug > -1)
            hmReplyToClient(caller,"This plugin doesn't seem to have any commands.");
        else if (criteria.size() > 0)
            hmReplyToClient(caller,"No commands found matching the criteria: '" + criteria + "'");
        else
            hmReplyToClient(caller,"No registered commands found.");
    }
    else
    {
        int start, stop;
        page++;
        do
        {
            page--;
            start = (page-1)*10;
            stop = page*10-1;
        }
        while (start >= list.size());
        int pages = list.size()/10;
        if (list.size() % 10)
            pages++;
        if (criteria.size() > 0)
            hmReplyToClient(caller,"Displaying page " + data2str("%i/%i",page,pages) + " matching: '" + criteria + "':");
        else
            hmReplyToClient(caller,"Displaying page " + data2str("%i/%i:",page,pages));
        for (auto it = list.begin()+start, ite = list.end();it != ite;++it)
        {
            hmReplyToClient(caller,*it);
            if (start == stop)
                break;
            start++;
        }
    }
    return 0;
}

int internalVersion(hmGlobal &global, string caller)
{
	hmReplyToClient(caller,string(VERSION) + " written by nigel.");
	hmReplyToClient(caller,global.hsVer + ", the vanilla Minecraft ModLoader written by nigel.");
	return 0;
}

int internalCredits(hmGlobal &global, string caller)
{
	hmReplyToClient(caller,string(VERSION) + " written by nigel.");
	hmReplyToClient(caller,global.hsVer + ", the vanilla Minecraft ModLoader written by nigel.");
	hmReplyToClient(caller,"Many thanks to SaintNewts and OSX for conceptual and technical advise.");
	hmReplyToClient(caller,"To highlight a few of the things making halfMod possible:");
	hmReplyToClient(caller,"SaintNewts: Running Minecraft in line buffered mode, allowing an instant file-less pipe directly into halfShell.");
	hmReplyToClient(caller,"OSX: Using dlfcn.h to dynamically load shared libraries, allowing the halfMod API to load and handle C++ plugins.");
	hmReplyToClient(caller,"SourceMod has been a hyuuuge influence over nearly every aspect of halfMod.");
	return 0;
}

int internalRcon(hmGlobal &global, vector<hmHandle> &plugins, vector<hmConsoleFilter> &filters, string cmd, string caller, string args)
{
	if ((hmGetPlayerFlags(caller) & FLAG_RCON) == 0)
	{
		hmReplyToClient(caller,"You do not have access to this command.");
		return 9;
	}
	if (args == "")
	{
		hmReplyToClient(caller,"Usage: " + cmd + " <command> [arguments ...]");
		return 8;
	}	
	processCmd(global,plugins,filters,gettok(args,1," "),caller,deltok(args,1," "),true);
	return 0;
}

int internalRehash(vector<hmConsoleFilter> &filters, string caller)
{
	if ((hmGetPlayerFlags(caller) & FLAG_CONFIG) == 0)
	{
		hmReplyToClient(caller,"You do not have access to this command.");
		return 9;
	}
	if (hashConsoleFilters(filters,CONFILTERPATH))
	{
		hmReplyToClient(caller,"Unable to load console filter config file . . .");
		return 1;
	}
	hmReplyToClient(caller,"Successfully reloaded the console filter file.");
	return 0;
}

void processTimers(vector<hmHandle> &plugins)
{
	time_t curTime = time(NULL);
	int n;
	size_t cap;
	for (vector<hmHandle>::iterator ita = plugins.begin(), ite = plugins.end();ita != ite;++ita)
	{
	    n = 0;
	    cap = ita->timers.capacity();
		for (vector<hmTimer>::iterator it = ita->timers.begin();it != ita->timers.end();)
		{
		    if (ita->invalidTimers)
		        ita->invalidTimers = false;
			if (it->last+it->interval <= curTime)
			{
				if ((*it->func)(*ita,it->args))
				{
				    if ((ita->invalidTimers) && (cap != ita->timers.capacity()))
				        it = ita->timers.erase(ita->timers.begin()+n);
				    else
					    it = ita->timers.erase(it);
				}
				else
				{
    				if ((ita->invalidTimers) && (cap != ita->timers.capacity()))
				        it = ita->timers.begin()+n;
					it->last = curTime;
					it++;
				}
			}
			else	it++;
			n++;
		}
	}
}
