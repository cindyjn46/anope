/* NickServ core functions
 *
 * (C) 2003-2009 Anope Team
 * Contact us at team@anope.org
 *
 * Please read COPYING and README for further details.
 *
 * Based on the original code of Epona by Lara.
 * Based on the original code of Services by Andy Church.
 *
 * $Id$
 *
 */
/*************************************************************************/

#include "module.h"

class CommandNSGroup : public Command
{
 public:
	CommandNSGroup() : Command("GROUP", 2, 2)
	{
		this->SetFlag(CFLAG_ALLOW_UNREGISTERED);
	}

	CommandReturn Execute(User *u, const std::vector<ci::string> &params)
	{
		NickAlias *na, *target;
		const char *nick = params[0].c_str();
		std::string pass = params[1].c_str();
		std::list<std::pair<std::string, std::string> >::iterator it;

		if (Config.NSEmailReg && findrequestnick(u->nick.c_str()))
		{
			notice_lang(Config.s_NickServ, u, NICK_REQUESTED);
			return MOD_CONT;
		}

		if (readonly)
		{
			notice_lang(Config.s_NickServ, u, NICK_GROUP_DISABLED);
			return MOD_CONT;
		}

		if (!ircdproto->IsNickValid(u->nick.c_str()))
		{
			notice_lang(Config.s_NickServ, u, NICK_X_FORBIDDEN, u->nick.c_str());
			return MOD_CONT;
		}

		if (Config.RestrictOperNicks)
		{
			for (it = Config.Opers.begin(); it != Config.Opers.end(); ++it)
			{
				std::string nick = it->first;

				if (stristr(u->nick.c_str(), nick.c_str()) && !is_oper(u))
				{
					notice_lang(Config.s_NickServ, u, NICK_CANNOT_BE_REGISTERED, u->nick.c_str());
					return MOD_CONT;
				}
			}
		}

		na = findnick(u->nick);
		if (!(target = findnick(nick)))
			notice_lang(Config.s_NickServ, u, NICK_X_NOT_REGISTERED, nick);
		else if (time(NULL) < u->lastnickreg + Config.NSRegDelay)
			notice_lang(Config.s_NickServ, u, NICK_GROUP_PLEASE_WAIT, (Config.NSRegDelay + u->lastnickreg) - time(NULL));
		else if (u->nc && u->nc->HasFlag(NI_SUSPENDED))
		{
			alog("%s: %s!%s@%s tried to use GROUP from SUSPENDED nick %s", Config.s_NickServ, u->nick.c_str(), u->GetIdent().c_str(), u->host, target->nick);
			notice_lang(Config.s_NickServ, u, NICK_X_SUSPENDED, u->nick.c_str());
		}
		else if (target && target->nc->HasFlag(NI_SUSPENDED))
		{
			alog("%s: %s!%s@%s tried to use GROUP from SUSPENDED nick %s", Config.s_NickServ, u->nick.c_str(), u->GetIdent().c_str(), u->host, target->nick);
			notice_lang(Config.s_NickServ, u, NICK_X_SUSPENDED, target->nick);
		}
		else if (target->HasFlag(NS_FORBIDDEN))
			notice_lang(Config.s_NickServ, u, NICK_X_FORBIDDEN, nick);
		else if (na && target->nc == na->nc)
			notice_lang(Config.s_NickServ, u, NICK_GROUP_SAME, target->nick);
		else if (na && na->nc != u->nc)
			notice_lang(Config.s_NickServ, u, NICK_IDENTIFY_REQUIRED, Config.s_NickServ);
		else if (Config.NSMaxAliases && (target->nc->aliases.count >= Config.NSMaxAliases) && !target->nc->IsServicesOper())
			notice_lang(Config.s_NickServ, u, NICK_GROUP_TOO_MANY, target->nick, Config.s_NickServ, Config.s_NickServ);
		else if (enc_check_password(pass, target->nc->pass) != 1)
		{
			alog("%s: Failed GROUP for %s!%s@%s (invalid password)", Config.s_NickServ, u->nick.c_str(), u->GetIdent().c_str(), u->host);
			notice_lang(Config.s_NickServ, u, PASSWORD_INCORRECT);
			bad_password(u);
		}
		else
		{
			/* If the nick is already registered, drop it.
			 * If not, check that it is valid.
			 */
			if (na)
				delete na;
			else
			{
				int prefixlen = strlen(Config.NSGuestNickPrefix);
				int nicklen = u->nick.length();

				if (nicklen <= prefixlen + 7 && nicklen >= prefixlen + 1 && stristr(u->nick.c_str(), Config.NSGuestNickPrefix) == u->nick.c_str() && strspn(u->nick.c_str() + prefixlen, "1234567890") == nicklen - prefixlen)
				{
					notice_lang(Config.s_NickServ, u, NICK_CANNOT_BE_REGISTERED, u->nick.c_str());
					return MOD_CONT;
				}
			}

			na = new NickAlias(u->nick, target->nc);

			if (na)
			{
				std::string last_usermask = u->GetIdent() + "@" + u->GetDisplayedHost();
				na->last_usermask = sstrdup(last_usermask.c_str());
				na->last_realname = sstrdup(u->realname);
				na->time_registered = na->last_seen = time(NULL);

				u->nc = na->nc;

				FOREACH_MOD(I_OnNickGroup, OnNickGroup(u, target));
				ircdproto->SetAutoIdentificationToken(u);

				alog("%s: %s!%s@%s makes %s join group of %s (%s) (e-mail: %s)", Config.s_NickServ, u->nick.c_str(), u->GetIdent().c_str(), u->host, u->nick.c_str(), target->nick, target->nc->display, (target->nc->email ? target->nc->email : "none"));
				notice_lang(Config.s_NickServ, u, NICK_GROUP_JOINED, target->nick);

				u->lastnickreg = time(NULL);

				check_memos(u);
			}
			else
			{
				alog("%s: makealias(%s) failed", Config.s_NickServ, u->nick.c_str());
				notice_lang(Config.s_NickServ, u, NICK_GROUP_FAILED);
			}
		}
		return MOD_CONT;
	}

	bool OnHelp(User *u, const ci::string &subcommand)
	{
		notice_help(Config.s_NickServ, u, NICK_HELP_GROUP);
		return true;
	}

	void OnSyntaxError(User *u, const ci::string &subcommand)
	{
		syntax_error(Config.s_NickServ, u, "GROUP", NICK_GROUP_SYNTAX);
	}
};

class CommandNSGList : public Command
{
 public:
	CommandNSGList() : Command("GLIST", 0, 1)
	{
	}

	CommandReturn Execute(User *u, const std::vector<ci::string> &params)
	{
		const char *nick = params.size() ? params[0].c_str() : NULL;

		NickCore *nc = u->nc;
		int i;

		if (nick && (stricmp(nick, u->nick.c_str()) && !u->nc->IsServicesOper()))
			notice_lang(Config.s_NickServ, u, ACCESS_DENIED, Config.s_NickServ);
		else if (nick && (!findnick(nick) || !(nc = findnick(nick)->nc)))
			notice_lang(Config.s_NickServ, u, !nick ? NICK_NOT_REGISTERED : NICK_X_NOT_REGISTERED, nick);
		else
		{
			time_t expt;
			struct tm *tm;
			char buf[BUFSIZE];
			int wont_expire;

			notice_lang(Config.s_NickServ, u, nick ? NICK_GLIST_HEADER_X : NICK_GLIST_HEADER, nc->display);
			for (i = 0; i < nc->aliases.count; ++i)
			{
				NickAlias *na2 = static_cast<NickAlias *>(nc->aliases.list[i]);
				if (na2->nc == nc)
				{
					if (!(wont_expire = na2->HasFlag(NS_NO_EXPIRE)))
					{
						expt = na2->last_seen + Config.NSExpire;
						tm = localtime(&expt);
						strftime_lang(buf, sizeof(buf), finduser(na2->nick), STRFTIME_DATE_TIME_FORMAT, tm);
					}
					notice_lang(Config.s_NickServ, u, u->nc->IsServicesOper() && !wont_expire ? NICK_GLIST_REPLY_ADMIN : NICK_GLIST_REPLY, wont_expire ? '!' : ' ', na2->nick, buf);
				}
			}
			notice_lang(Config.s_NickServ, u, NICK_GLIST_FOOTER, nc->aliases.count);
		}
		return MOD_CONT;
	}


	bool OnHelp(User *u, const ci::string &subcommand)
	{
		if (u->nc && u->nc->IsServicesOper())
			notice_help(Config.s_NickServ, u, NICK_SERVADMIN_HELP_GLIST);
		else
			notice_help(Config.s_NickServ, u, NICK_HELP_GLIST);

		return true;
	}
};

class NSGroup : public Module
{
 public:
	NSGroup(const std::string &modname, const std::string &creator) : Module(modname, creator)
	{
		this->SetAuthor("Anope");
		this->SetVersion("$Id$");
		this->SetType(CORE);

		this->AddCommand(NICKSERV, new CommandNSGroup());
		this->AddCommand(NICKSERV, new CommandNSGList());

		ModuleManager::Attach(I_OnNickServHelp, this);
	}
	void OnNickServHelp(User *u)
	{
		notice_lang(Config.s_NickServ, u, NICK_HELP_CMD_GROUP);
		notice_lang(Config.s_NickServ, u, NICK_HELP_CMD_GLIST);
	}
};

MODULE_INIT(NSGroup)
