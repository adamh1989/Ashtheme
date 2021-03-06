/*
 * Copyright (c) 2005 William Pitcock <nenolod -at- nenolod.net>
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Allows banning certain email addresses from registering as
 * well as AKILLing on certain matched attempts. (Helps with
 * botnets throwing REGISTER at services during floods)
 *
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
    "nickserv/badmail", true, _modinit, _moddeinit,
    PACKAGE_STRING,
    "Atheme Development Group <http://www.atheme.net>"
);

static void check_registration(hook_user_register_check_t *hdata);
static void ns_cmd_badmail(sourceinfo_t *si, int parc, char *parv[]);

static void write_bedb(database_handle_t *db);
static void write_bedb2(database_handle_t *db);
static void db_h_be(database_handle_t *db, const char *type);
static void db_h_be2(database_handle_t *db, const char *type);

command_t ns_badmail = { "BADMAIL", N_("Disallows registrations from certain email addresses."), PRIV_USER_ADMIN, 4, ns_cmd_badmail, { .path = "nickserv/badmail" } };

struct badmail_ {
    char *mail;
    char *action;
    time_t mail_ts;
    char *creator;
    char *reason;
};

typedef struct badmail_ badmail_t;

mowgli_list_t ns_maillist;
mowgli_list_t ns_maillist2;

void _modinit(module_t *m)
{
    if (!module_find_published("backend/opensex")) {
        slog(LG_INFO, "Module %s requires use of the OpenSEX database backend, refusing to load.", m->name);
        m->mflags = MODTYPE_FAIL;
        return;
    }

    hook_add_event("user_can_register");
    hook_add_user_can_register(check_registration);
    hook_add_db_write(write_bedb);
    hook_add_db_write(write_bedb2);

    db_register_type_handler("BE", db_h_be);
    db_register_type_handler("BE2", db_h_be2);

    service_named_bind_command("nickserv", &ns_badmail);
}

void _moddeinit(module_unload_intent_t intent)
{
    hook_del_user_can_register(check_registration);
    hook_del_db_write(write_bedb);
    hook_del_db_write(write_bedb2);

    db_unregister_type_handler("BE");
    db_unregister_type_handler("BE2");

    service_named_unbind_command("nickserv", &ns_badmail);
}

static void write_bedb(database_handle_t *db)
{
    mowgli_node_t *n;

    MOWGLI_ITER_FOREACH(n, ns_maillist.head) {
        badmail_t *l = n->data;

        db_start_row(db, "BE");
        db_write_word(db, l->mail);
        db_write_time(db, l->mail_ts);
        db_write_word(db, l->creator);
        db_write_str(db, l->reason);
        db_commit_row(db);
    }
}

static void write_bedb2(database_handle_t *db)
{
    mowgli_node_t *n;

    MOWGLI_ITER_FOREACH(n, ns_maillist2.head) {
        badmail_t *l = n->data;

        db_start_row(db, "BE2");
        db_write_word(db, l->mail);
        db_write_word(db, l->action);
        db_write_time(db, l->mail_ts);
        db_write_word(db, l->creator);
        db_write_str(db, l->reason);
        db_commit_row(db);
    }
}

static void db_h_be(database_handle_t *db, const char *type)
{
    const char *mail = db_sread_word(db);
    time_t mail_ts = db_sread_time(db);
    const char *creator = db_sread_word(db);
    const char *reason = db_sread_str(db);

    badmail_t *l = smalloc(sizeof(badmail_t));
    l->mail = sstrdup(mail);
    l->action = "REJECT";
    l->mail_ts = mail_ts;
    l->creator = sstrdup(creator);
    l->reason = sstrdup(reason);
    mowgli_node_add(l, mowgli_node_create(), &ns_maillist);
}

static void db_h_be2(database_handle_t *db, const char *type)
{
    const char *mail = db_sread_word(db);
    const char *action = db_sread_word(db);
    time_t mail_ts = db_sread_time(db);
    const char *creator = db_sread_word(db);
    const char *reason = db_sread_str(db);

    badmail_t *l = smalloc(sizeof(badmail_t));
    l->mail = sstrdup(mail);
    l->action = sstrdup(action);
    l->mail_ts = mail_ts;
    l->creator = sstrdup(creator);
    l->reason = sstrdup(reason);
    mowgli_node_add(l, mowgli_node_create(), &ns_maillist2);
}

static void check_registration(hook_user_register_check_t *hdata)
{
    mowgli_node_t *n;
    badmail_t *l;
    kline_t *k;
    user_t *u = hdata->si->su;

    if (hdata->approved)
        return;

    if (!u) {
        command_fail(hdata->si, fault_noprivs, "Sorry, we do not accept registrations with email addresses from that domain.");
        hdata->approved = 1;
        return;
    }


    MOWGLI_ITER_FOREACH(n, ns_maillist.head) {
        l = n->data;

        if (!match(l->mail, hdata->email)) {
            if (l->action == "AKILL") {
                if (hdata->si->su->nick != NULL) {
                    if (is_autokline_exempt(hdata->si->su)) {
                        command_success_nodata(hdata->si, _("REGISTER:BADMAIL: Not akilling exempt user %s!%s@%s"), hdata->si->su->nick, hdata->si->su->user, hdata->si->su->host);
                    } else {
                        command_fail(hdata->si, fault_noprivs, "Sorry, we do not accept registrations with email addresses from that domain. Goodbye.");
                        k = kline_add("*", hdata->si->su->host, "(BADMAIL) We do not accept registrations with email addresses from that domain.", config_options.kline_time, "BADMAIL");
                        hdata->approved = 1;
                        slog(LG_INFO, "REGISTER:BADMAIL:\2AKILL\2 %s (\2%s\2) tried to REGISTER with \2%s\2. (AKILL Activated)", hdata->account, hdata->si->su->nick, hdata->email);

                    }
                }
            }

            if (l->action == NULL || l->action == "REJECT") {
                command_fail(hdata->si, fault_noprivs, "Sorry, we do not accept registrations with email addresses from that domain. Use another address.");
                hdata->approved = 1;
                slog(LG_INFO, "REGISTER:BADMAIL:\2REJECT\2 %s (\2%s\2) tried to REGISTER with \2%s\2. (Rejected)", hdata->account, hdata->si->su != NULL ? hdata->si->su->nick : get_source_name(hdata->si), hdata->email);
                return;
            }
        }
    }
    MOWGLI_ITER_FOREACH(n, ns_maillist2.head) {
        l = n->data;

        if (!match(l->mail, hdata->email)) {
            if (l->action == "AKILL") {
                if (hdata->si->su->nick != NULL) {
                    if (is_autokline_exempt(hdata->si->su)) {
                        command_success_nodata(hdata->si, _("REGISTER:BADMAIL: Not akilling exempt user %s!%s@%s"), hdata->si->su->nick, hdata->si->su->user, hdata->si->su->host);
                    } else {
                        command_fail(hdata->si, fault_noprivs, "Sorry, we do not accept registrations with email addresses from that domain. Goodbye.");
                        k = kline_add("*", hdata->si->su->host, "(BADMAIL) We do not accept registrations with email addresses from that domain.", config_options.kline_time, "BADMAIL");
                        hdata->approved = 1;
                        slog(LG_INFO, "REGISTER:BADMAIL:\2AKILL\2 %s (\2%s\2) tried to REGISTER with \2%s\2. (AKILL Activated)", hdata->account, hdata->si->su->nick, hdata->email);

                    }
                }
            }

            if (l->action == NULL || l->action == "REJECT") {
                command_fail(hdata->si, fault_noprivs, "Sorry, we do not accept registrations with email addresses from that domain. Use another address.");
                hdata->approved = 1;
                slog(LG_INFO, "REGISTER:BADMAIL:\2REJECT\2 %s (\2%s\2) tried to REGISTER with \2%s\2. (Rejected)", hdata->account, hdata->si->su != NULL ? hdata->si->su->nick : get_source_name(hdata->si), hdata->email);
                return;
            }
        }
    }
}

static void ns_cmd_badmail(sourceinfo_t *si, int parc, char *parv[])
{
    char *action = parv[0];
    char *email = parv[1];
    char *baction = parv[2];
    char *reason = parv[3];
    mowgli_node_t *n, *tn;
    badmail_t *l;

    if (!action) {
        command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "BADMAIL");
        command_fail(si, fault_needmoreparams, _("Syntax: BADMAIL ADD|DEL|LIST|TEST [parameters]"));
        return;
    }

    if (!strcasecmp("ADD", action)) {
        if (!email || !baction || !reason) {
            command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "BADMAIL");
            command_fail(si, fault_needmoreparams, _("Syntax: BADMAIL ADD <email> <action> <reason>"));
            return;
        }

        if (si->smu == NULL) {
            command_fail(si, fault_noprivs, _("You are not logged in."));
            return;
        }

        if (!strcasecmp("REJECT", baction)) {
            /* search for it */
            MOWGLI_ITER_FOREACH(n, ns_maillist.head) {
                l = n->data;

                if (!irccasecmp(l->mail, email)) {
                    command_success_nodata(si, _("Email \2%s\2 has already been banned."), email);
                    return;
                }
            }

            /* search for it in new bademail lists */
            MOWGLI_ITER_FOREACH(n, ns_maillist2.head) {
                l = n->data;

                if (!irccasecmp(l->mail, email)) {
                    command_success_nodata(si, _("Email \2%s\2 has already been banned."), email);
                    return;
                }
            }

            l = smalloc(sizeof(badmail_t));
            l->mail = sstrdup(email);
            l->action = "REJECT";
            l->mail_ts = CURRTIME;;
            l->creator = sstrdup(get_source_name(si));
            l->reason = sstrdup(reason);

            logcommand(si, CMDLOG_ADMIN, "BADMAIL:ADD: \2%s\2 Action: \2%s\2 (Reason: \2%s\2)", email, baction, reason);

            n = mowgli_node_create();
            mowgli_node_add(l, n, &ns_maillist2);

            command_success_nodata(si, _("You have banned email address \2%s\2."), email);
            return;
        }
        if (!strcasecmp("AKILL", baction)) {
            /* search for it */
            MOWGLI_ITER_FOREACH(n, ns_maillist.head) {
                l = n->data;

                if (!irccasecmp(l->mail, email)) {
                    command_success_nodata(si, _("Email \2%s\2 has already been banned."), email);
                    return;
                }
            }

            /* again, search for it in new badmail lists */
            MOWGLI_ITER_FOREACH(n, ns_maillist2.head) {
                l = n->data;

                if (!irccasecmp(l->mail, email)) {
                    command_success_nodata(si, _("Email \2%s\2 has already been banned."), email);
                    return;
                }
            }

            l = smalloc(sizeof(badmail_t));
            l->mail = sstrdup(email);
            l->action = "AKILL";
            l->mail_ts = CURRTIME;;
            l->creator = sstrdup(get_source_name(si));
            l->reason = sstrdup(reason);

            logcommand(si, CMDLOG_ADMIN, "BADMAIL:ADD: \2%s\2 Action: \2%s\2 (Reason: \2%s\2)", email, baction, reason);

            n = mowgli_node_create();
            mowgli_node_add(l, n, &ns_maillist2);

            command_success_nodata(si, _("You have banned email address \2%s\2."), email);
            return;
        } else {
            command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "BADMAIL");
            command_fail(si, fault_needmoreparams, _("Syntax: BADMAIL ADD <email> <AKILL/REJECT> <reason>"));
            return;
        }
    }
    if (!strcasecmp("DEL", action)) {
        if (!email) {
            command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "BADMAIL");
            command_fail(si, fault_needmoreparams, _("Syntax: BADMAIL DEL <email>"));
            return;
        }

        MOWGLI_ITER_FOREACH_SAFE(n, tn, ns_maillist.head) {
            l = n->data;

            if (!irccasecmp(l->mail, email)) {
                logcommand(si, CMDLOG_ADMIN, "BADMAIL:DEL: \2%s\2", l->mail);

                mowgli_node_delete(n, &ns_maillist);

                free(l->mail);
                free(l->creator);
                free(l->reason);
                free(l);

                command_success_nodata(si, _("You have unbanned email address \2%s\2."), email);
                return;
            }
        }

        /* search for it in new badmail lists */
        MOWGLI_ITER_FOREACH_SAFE(n, tn, ns_maillist2.head) {
            l = n->data;

            if (!irccasecmp(l->mail, email)) {
                logcommand(si, CMDLOG_ADMIN, "BADMAIL:DEL: \2%s\2", l->mail);

                mowgli_node_delete(n, &ns_maillist2);

                free(l->mail);
                free(l->creator);
                free(l->reason);
                free(l);

                command_success_nodata(si, _("You have unbanned email address \2%s\2."), email);
                return;
            }
        }

        command_success_nodata(si, _("Email pattern \2%s\2 not found in badmail database."), email);
        return;
    }
    if (!strcasecmp("LIST", action)) {
        char buf[BUFSIZE];
        struct tm tm;
        unsigned long count = 0;

        MOWGLI_ITER_FOREACH(n, ns_maillist.head) {
            l = n->data;

            if ((!email) || !match(email, l->mail)) {
                count++;
                tm = *localtime(&l->mail_ts);
                strftime(buf, BUFSIZE, TIME_FORMAT, &tm);
                command_success_nodata(si, _("Email: \2%s\2, Action: \2REJECT\2 Reason: \2%s\2 (%s - %s)"), l->mail, l->reason, l->creator, buf);
            }
        }
        MOWGLI_ITER_FOREACH(n, ns_maillist2.head) {
            l = n->data;

            if ((!email) || !match(email, l->mail)) {
                if (l->action == NULL) {
                    count++;
                    tm = *localtime(&l->mail_ts);
                    strftime(buf, BUFSIZE, TIME_FORMAT, &tm);
                    command_success_nodata(si, _("Email: \2%s\2, Action: \2REJECT\2 Reason: \2%s\2 (%s - %s)"), l->mail, l->reason, l->creator, buf);
                } else {
                    count++;
                    tm = *localtime(&l->mail_ts);
                    strftime(buf, BUFSIZE, TIME_FORMAT, &tm);
                    command_success_nodata(si, _("Email: \2%s\2, Action: \2%s\2 Reason: \2%s\2 (%s - %s)"), l->mail, l->action, l->reason, l->creator, buf);
                }
            }
        }
        if (email && !count)
            command_success_nodata(si, _("No entries matching pattern \2%s\2 found in badmail database."), email);
        else
            command_success_nodata(si, _("End of list."));

        if (email)
            logcommand(si, CMDLOG_GET, "BADMAIL:LIST: \2%s\2 (\2%ld\2 matches)", email, count);
        else
            logcommand(si, CMDLOG_GET, "BADMAIL:LIST (\2%ld\2 matches)", count);
        return;
    }
    if (!strcasecmp("TEST", action)) {
        char buf[BUFSIZE];
        struct tm tm;
        unsigned long count = 0;

        if (!email) {
            command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "BADMAIL");
            command_fail(si, fault_needmoreparams, _("Syntax: BADMAIL TEST <email>"));
            return;
        }

        MOWGLI_ITER_FOREACH(n, ns_maillist.head) {
            l = n->data;

            if (!match(l->mail, email)) {
                count++;
                tm = *localtime(&l->mail_ts);
                strftime(buf, BUFSIZE, TIME_FORMAT, &tm);
                command_success_nodata(si, _("Email: \2%s\2, Reason: \2%s\2 (%s - %s)"),
                                       l->mail, l->reason, l->creator, buf);
            }
        }
        if (count)
            command_success_nodata(si, _("%ld badmail pattern(s) disallowing \2%s\2 found."), count, email);
        else
            command_success_nodata(si, _("\2%s\2 is not listed in the badmail database."), email);

        logcommand(si, CMDLOG_GET, "BADMAIL:TEST: \2%s\2 (\2%ld\2 matches)", email, count);
        return;
    } else {
        command_fail(si, fault_badparams, STR_INVALID_PARAMS, "BADMAIL");
        return;
    }
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
