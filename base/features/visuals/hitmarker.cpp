#include "visuals.h"

void CVisuals::ManageHitmarkers( ) {
    if ( !ctx.m_pLocal || ctx.m_pLocal->IsDead( ) ) {
        hits.clear( );
        return;
    }

    hits.erase(
        std::remove_if(
            hits.begin( ), hits.end( ),
            [ & ]( const std::shared_ptr<hitmarker_t>& hit ) -> bool {
                return hit->time + 1.6f < Interfaces::Globals->flRealTime;
            }
        ),
        hits.end( )
                );

    for ( const auto& hit : hits ) {
        Math::WorldToScreen( hit->position, hit->screen );

        if ( hit->screen.IsZero( ) )
            continue;

        // shared timing: hold, then ease out over the last half second
        const float age{ Interfaces::Globals->flRealTime - hit->time };
        hit->alpha = 1.f - Anim::EaseIn( std::clamp( ( age - 1.1f ) / 0.5f, 0.f, 1.f ) );
        hit->step = age;

        DrawHitMarker( hit );
    }
}

void CVisuals::DrawHitMarker( const std::shared_ptr<hitmarker_t>& hit ) {
    if ( hit->alpha > 0 ) {
        // fresh hits flash in the accent colour, then settle to cream
        const auto flash{ std::clamp( 1.f - ( Interfaces::Globals->flRealTime - hit->time ) * 4.f, 0.f, 1.f ) };
        const auto col{ Color( 255, static_cast< int >( 236 - 87 * flash ), static_cast< int >( 214 - 168 * flash ), static_cast< int >( 255 * hit->alpha ) ) };
        const auto outlineCol{ Color( 10, 10, 10, static_cast< int >( 255 * hit->alpha ) ) };

        // TODO: fix bottom pixel of each line....
        auto DrawOutlinedLine{ [ & ]( Vector2D pos1, Vector2D pos2, bool left ) {
            // left
            Render::Line( pos1 - Vector2D{ 1, 0 }, pos2 - Vector2D{ 1, 0 }, outlineCol );

            // right
            Render::Line( pos1 - Vector2D{ -1, 0 }, pos2 - Vector2D{ -1, 0 }, outlineCol );

            // middle longer
            Render::Line( pos1 - Vector2D{ 0, 1 }, pos2 + Vector2D{ 0, 1 }, outlineCol );

            Render::Line( pos1, pos2, col );
        } };


        // welcome to hell!
        if ( Config::Get<bool>( Vars.MiscWorldHitmarker ) ) {
            const auto& linesize{ Config::Get<int>( Vars.MiscWorldHitmarkerSize ) };
            auto lineOffset{ Config::Get<int>( Vars.MiscWorldHitmarkerGap ) };
            // top left
            DrawOutlinedLine( { hit->screen.x - linesize - lineOffset, hit->screen.y - linesize - lineOffset }, { hit->screen.x - lineOffset, hit->screen.y - lineOffset }, true );

            // top right
            DrawOutlinedLine( { hit->screen.x + linesize + lineOffset, hit->screen.y - linesize - lineOffset }, { hit->screen.x + lineOffset, hit->screen.y - lineOffset }, false );

            // bottom left
            DrawOutlinedLine( { hit->screen.x - linesize - lineOffset, hit->screen.y + linesize + lineOffset }, { hit->screen.x - lineOffset, hit->screen.y + lineOffset }, true );

            // bottom right
            DrawOutlinedLine( { hit->screen.x + linesize + lineOffset, hit->screen.y + linesize + lineOffset } , { hit->screen.x + lineOffset, hit->screen.y + lineOffset }, false );
        }

        if ( Config::Get<bool>( Vars.MiscScreenHitmarker ) ) {
            const auto& linesize{ Config::Get<int>( Vars.MiscScreenHitmarkerSize ) };
            const auto& lineOffset{ Config::Get<int>( Vars.MiscScreenHitmarkerGap ) };

            const auto center{ ctx.m_ve2ScreenSize / 2 };
            // top left
            DrawOutlinedLine( { center.x - linesize - lineOffset, center.y - linesize - lineOffset }, { center.x - lineOffset, center.y - lineOffset }, true );

            // top right
            DrawOutlinedLine( { center.x + linesize + lineOffset, center.y - linesize - lineOffset }, { center.x + lineOffset, center.y - lineOffset }, false );

            // bottom left
            DrawOutlinedLine( { center.x - linesize - lineOffset, center.y + linesize + lineOffset }, { center.x - lineOffset, center.y + lineOffset }, true );

            // bottom right
            DrawOutlinedLine( { center.x + linesize + lineOffset, center.y + linesize + lineOffset }, { center.x + lineOffset, center.y + lineOffset }, false );
        }

        if ( Config::Get<bool>( Vars.MiscDamageMarker ) ) {
            // damage numbers: rise and drift a little, colour by what was hit ( kill red, head orange, body = picked colour )
            const float age{ hit->step };
            Color base{ Config::Get<Color>( Vars.MiscDamageMarkerCol ) };
            if ( hit->lethal )
                base = Color( 255, 72, 72, static_cast< int >( base.Get<COLOR_A>( ) ) );
            else if ( hit->hitgroup == HITGROUP_HEAD )
                base = Color( 255, 149, 46, static_cast< int >( base.Get<COLOR_A>( ) ) );

            const float pop{ Anim::EaseOutBack( std::min( age / 0.16f, 1.f ) ) };
            const float rise{ 14.f + 30.f * Anim::EaseOut( age / 1.6f ) + ( 1.f - std::min( pop, 1.f ) ) * 6.f };
            const Vector2D textPos{ hit->screen.x + hit->drift * Anim::EaseOut( age / 1.6f ), hit->screen.y - rise };

            // counts up quickly instead of appearing at full value
            const auto displayDmg{ static_cast< int >( std::ceil( hit->damage * Anim::EaseOut( std::min( age / 0.25f, 1.f ) ) ) ) };
            const auto text{ std::to_string( displayDmg ) };

            const int alpha{ static_cast< int >( std::clamp( base.Get<COLOR_A>( ) * hit->alpha * std::clamp( pop, 0.f, 1.f ), 0.f, 255.f ) ) };
            Render::Text( Fonts::DamageMarker, textPos + Vector2D( 1.f, 1.f ), Color( 0, 0, 0, alpha * 2 / 3 ), FONT_CENTER, text.c_str( ) );
            Render::Text( Fonts::DamageMarker, textPos, base.Set<COLOR_A>( alpha ), FONT_CENTER, text.c_str( ) );
        }
    }
}